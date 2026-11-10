#include "gs/script/TestRun.h"

#include "gs/script/Report.h"
#include "gs/script/ScriptHost.h"
#include "gs/script/Session.h"

#include <gs/base/Clock.h>
#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <system_error>
#include <utility>

namespace gs::script {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr std::string_view kSessionFile = "session.json";
constexpr std::string_view kProfileFile = "profile.json";
constexpr std::size_t kMostTestBytes = std::size_t{64} << 20;

std::string utf8(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

fs::path pathOf(std::string_view text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string_view levelName(log::Level level)
{
    switch (level) {
    case log::Level::Trace:
        return "trace";
    case log::Level::Debug:
        return "debug";
    case log::Level::Info:
        return "info";
    case log::Level::Warning:
        return "warn";
    case log::Level::Error:
        return "error";
    }
    return "info";
}

bool failing(ReportKind kind) noexcept
{
    return kind == ReportKind::ScriptFailed || kind == ReportKind::TaskFailed || kind == ReportKind::TaskInterrupted
        || kind == ReportKind::UnhandledRejection || kind == ReportKind::MemoryLimit;
}

std::string missingEntry(const fs::path& folder)
{
    std::error_code error;
    return fs::is_regular_file(folder / pathOf(kProfileFile), error) ? "the profile folder has no logic.ts or logic.js"
                                                                     : "the project has no src/main.ts or src/main.js";
}

std::optional<std::uint64_t> seedOf(const Json& value)
{
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() > 0)) {
        return std::nullopt;
    }
    const auto seed = value.get<std::uint64_t>();
    return seed == 0 ? std::nullopt : std::optional<std::uint64_t>(seed);
}

std::string specProblem(const Json& spec, bool update)
{
    if (spec.is_discarded() || !spec.is_object()) {
        return "the test is not a JSON object";
    }
    if (!spec.contains("session") || !spec.at("session").is_string()) {
        return "$.session must be the path of a session file or a recording folder, relative to the test or absolute";
    }
    if (spec.contains("until")
        && (!spec.at("until").is_number() || spec.at("until").get<double>() <= 0.0
            || spec.at("until").get<double>() > kLongestReplayMs)) {
        return "$.until must be a number of milliseconds";
    }
    if (spec.contains("seed") && !seedOf(spec.at("seed"))) {
        return "$.seed must be a whole number from 1";
    }
    if (!update && (!spec.contains("expect") || !spec.at("expect").is_array())) {
        return "$.expect must be a list of output lines; run gs test --update to write it";
    }
    return {};
}

}

fs::path sessionFileOf(const fs::path& sessionOrRecording)
{
    std::error_code error;
    return fs::is_directory(sessionOrRecording, error) ? sessionOrRecording / pathOf(kSessionFile) : sessionOrRecording;
}

ReplayRun replaySession(const ReplayRequest& request, const ReplayKit& kit)
{
    ReplayRun run;
    if (request.entry.empty()) {
        run.problem = missingEntry(request.folder);
        return run;
    }
    const fs::path sessionFile = sessionFileOf(request.session);
    const auto session = loadSession(sessionFile);
    if (!session) {
        run.problem = "session " + utf8(sessionFile) + ": " + session.error();
        return run;
    }
    ReplayProfile profile;
    fs::path profileFile;
    std::error_code error;
    if (!session->profile.empty()) {
        profileFile = sessionFile.parent_path() / pathOf(session->profile);
    } else if (fs::is_regular_file(request.folder / pathOf(kProfileFile), error)) {
        profileFile = request.folder / pathOf(kProfileFile);
    }
    if (!profileFile.empty() && kit.profile) {
        auto loaded = kit.profile(profileFile);
        if (!loaded) {
            run.problem = "profile " + utf8(profileFile) + ": " + loaded.error();
            return run;
        }
        profile = std::move(*loaded);
    }
    ManualClock clock;
    RecordingServices services(clock, session->device, std::move(profile.controls), std::move(profile.states));
    std::unique_ptr<ReplayVision> vision;
    if (!session->video.empty()) {
        const fs::path video = sessionFile.parent_path() / pathOf(session->video);
        if (kit.vision) {
            vision = kit.vision(request.folder, video, session->videoStartPtsUs);
        }
        if (vision == nullptr) {
            run.problem = "session " + utf8(sessionFile) + ": the picture " + utf8(video) + " cannot be read";
            return run;
        }
        services.setVision(vision.get());
    }
    bool failed = false;
    const ScriptHost::Sink sink = [&services, &failed](const Report& report) {
        if (report.kind == ReportKind::Log) {
            services.note(std::format("log {} {}", levelName(report.level), report.message));
        } else if (failing(report.kind)) {
            failed = true;
            services.note("error " + describe(report));
        }
    };
    ReplayOptions options;
    options.source =
        ScriptSource{.folder = request.folder, .entry = request.entry, .modules = controlModules(request.folder)};
    options.events = session->events;
    const double untilMs = request.untilMs.value_or(
        session->durationMs > 0 ? static_cast<double>(session->durationMs) : kDefaultReplayMs);
    options.until = Microseconds{static_cast<std::int64_t>(std::llround(untilMs * 1000.0))};
    options.seed = request.seed;
    options.wallEpochMs = session->startedAtMs;
    options.frameRate = session->frameRate;
    run.result = replay(options, services, clock, sink);
    run.lines = services.lines();
    run.machines = services.machineStates();
    run.ok = run.result.loaded && !failed;
    return run;
}

std::vector<std::string> outputLines(const std::vector<ReplayLine>& lines)
{
    std::vector<std::string> shown;
    for (const ReplayLine& line : lines) {
        if (line.output) {
            shown.push_back(formatLine(line));
        }
    }
    return shown;
}

std::vector<fs::path> testFiles(const fs::path& folder)
{
    std::vector<fs::path> files;
    std::error_code error;
    for (auto at = fs::directory_iterator(folder / pathOf(kTestsFolder), error); !error && at != fs::directory_iterator();
         at.increment(error)) {
        if (at->is_regular_file(error) && utf8(at->path().filename()).ends_with(kTestSuffix)) {
            files.push_back(at->path());
        }
    }
    std::ranges::sort(files);
    return files;
}

TestOutcome runTest(const fs::path& file, const fs::path& folder, std::string_view entry, bool update,
                    const ReplayKit& kit)
{
    TestOutcome outcome;
    outcome.file = file;
    outcome.name = utf8(file.filename());
    if (outcome.name.ends_with(kTestSuffix)) {
        outcome.name.resize(outcome.name.size() - kTestSuffix.size());
    }
    std::string text;
    Json spec = platform::readFile(file, kMostTestBytes, text) ? Json() : Json::parse(text, nullptr, false);
    std::string problem = specProblem(spec, update);
    ReplayRun run;
    if (problem.empty()) {
        const fs::path given = pathOf(spec.at("session").get_ref<const std::string&>());
        const fs::path session = given.is_absolute() ? given : file.parent_path() / given;
        std::error_code error;
        if (!fs::exists(sessionFileOf(session), error)) {
            outcome.missingRecording = true;
            problem = "the recording is no longer there: " + utf8(session);
        } else {
            ReplayRequest request{.folder = folder, .entry = std::string(entry), .session = session};
            if (spec.contains("until")) {
                request.untilMs = spec.at("until").get<double>();
            }
            if (spec.contains("seed")) {
                request.seed = seedOf(spec.at("seed")).value_or(1);
            }
            run = replaySession(request, kit);
            problem = run.problem;
        }
    }
    outcome.lines = run.lines;
    if (!problem.empty()) {
        outcome.verdict = TestVerdict::Error;
        outcome.detail = std::move(problem);
        return outcome;
    }
    if (!run.ok) {
        outcome.verdict = TestVerdict::Fail;
        outcome.logicFailed = true;
        outcome.detail = "the script failed while replaying";
        return outcome;
    }
    const std::vector<std::string> actual = outputLines(run.lines);
    if (update) {
        spec["expect"] = actual;
        if (platform::writeFileAtomically(file, spec.dump(2) + "\n")) {
            outcome.verdict = TestVerdict::Error;
            outcome.detail = "cannot write the test file";
        } else {
            outcome.verdict = TestVerdict::Updated;
        }
        return outcome;
    }
    std::vector<std::string> expected;
    for (const Json& line : spec.at("expect")) {
        expected.push_back(line.is_string() ? line.get<std::string>() : line.dump());
    }
    if (actual == expected) {
        return outcome;
    }
    const auto mismatch = std::ranges::mismatch(expected, actual);
    outcome.verdict = TestVerdict::Fail;
    outcome.line = static_cast<std::size_t>(mismatch.in1 - expected.begin()) + 1;
    if (mismatch.in1 != expected.end()) {
        outcome.expected = *mismatch.in1;
    }
    if (mismatch.in2 != actual.end()) {
        outcome.actual = *mismatch.in2;
    }
    outcome.detail = std::format("output line {}: expected \"{}\", got \"{}\"", outcome.line,
                                 outcome.expected.value_or("(nothing more)"), outcome.actual.value_or("(nothing more)"));
    return outcome;
}

std::string testText(std::string_view session, const std::vector<std::string>& expect)
{
    Json spec = Json::object();
    spec["session"] = std::string(session);
    spec["expect"] = expect;
    return spec.dump(2) + "\n";
}

std::string_view verdictName(TestVerdict verdict) noexcept
{
    switch (verdict) {
    case TestVerdict::Pass:
        return "pass";
    case TestVerdict::Fail:
        return "fail";
    case TestVerdict::Error:
        return "error";
    case TestVerdict::Updated:
        return "updated";
    }
    return "error";
}

}
