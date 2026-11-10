#include "ScriptTools.h"

#include "ScriptProject.h"

#include <gs/base/Expected.h>
#include <gs/record/RecordedVision.h>
#include <gs/script/Replay.h>
#include <gs/script/TestRun.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cwchar>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gs::cli {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

std::optional<double> numberOf(std::wstring_view text)
{
    const std::wstring value(text);
    wchar_t* end = nullptr;
    const double number = std::wcstod(value.c_str(), &end);
    if (value.empty() || end != value.c_str() + value.size() || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

std::string joined(const std::vector<std::string>& parts)
{
    std::string text;
    for (const std::string& part : parts) {
        text += (text.empty() ? "" : "; ") + part;
    }
    return text;
}

script::ReplayKit replayKit()
{
    return script::ReplayKit{
        .profile = [](const fs::path& file) -> Expected<script::ReplayProfile, std::string> {
            auto loaded = loadProjectProfile(file);
            if (!loaded) {
                return makeUnexpected(joined(loaded.error()));
            }
            return script::ReplayProfile{.controls = std::move(loaded->controls), .states = std::move(loaded->states)};
        },
        .vision = [](const fs::path& folder, const fs::path& video,
                     std::int64_t videoStartPtsUs) -> std::unique_ptr<script::ReplayVision> {
            auto vision = std::make_unique<record::RecordedVision>(folder, video, videoStartPtsUs);
            if (!vision->opened()) {
                return nullptr;
            }
            return vision;
        }};
}

std::string endText(const script::ReplayRun& run)
{
    return std::format("ended at {} ms{}", static_cast<double>(run.result.ended.count()) / 1000.0,
                       run.result.reachedLimit ? " (time limit)" : "");
}

std::string_view headingOf(script::TestVerdict verdict)
{
    switch (verdict) {
    case script::TestVerdict::Pass:
        return "PASS ";
    case script::TestVerdict::Updated:
        return "UPDATED ";
    case script::TestVerdict::Fail:
        return "FAIL ";
    case script::TestVerdict::Error:
        return "ERROR ";
    }
    return "ERROR ";
}

int runUsage()
{
    std::cerr << "usage: gs run <folder> --replay <session.json | recording.gsrec> [--until <ms>] [--seed <n>] [--json]\n";
    return kUsage;
}

int testUsage()
{
    std::cerr << "usage: gs test <folder> [--update] [--json]\n";
    return kUsage;
}

}

int runReplayCommand(const std::vector<std::wstring_view>& args)
{
    if (args.empty()) {
        return runUsage();
    }
    script::ReplayRequest request;
    request.folder = fs::path(args[0]);
    bool json = false;
    bool haveSession = false;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::wstring_view option = args[index];
        if (option == L"--json") {
            json = true;
        } else if (option == L"--replay" && index + 1 < args.size()) {
            request.session = fs::path(args[++index]);
            haveSession = true;
        } else if (option == L"--until" && index + 1 < args.size()) {
            const auto until = numberOf(args[++index]);
            if (!until || *until <= 0.0 || *until > script::kLongestReplayMs) {
                return runUsage();
            }
            request.untilMs = *until;
        } else if (option == L"--seed" && index + 1 < args.size()) {
            const auto seed = numberOf(args[++index]);
            if (!seed || *seed < 1.0 || *seed != std::floor(*seed) || *seed > 9.0e15) {
                return runUsage();
            }
            request.seed = static_cast<std::uint64_t>(*seed);
        } else {
            return runUsage();
        }
    }
    if (!haveSession) {
        return runUsage();
    }
    request.entry = entryOf(request.folder).value_or(std::string());
    const script::ReplayRun run = script::replaySession(request, replayKit());
    if (!run.problem.empty()) {
        std::cerr << "gs run: " << run.problem << '\n';
        return kProblems;
    }
    if (json) {
        Json lines = Json::array();
        for (const script::ReplayLine& line : run.lines) {
            lines.push_back({{"at", static_cast<double>(line.at.count()) / 1000.0},
                             {"text", line.text},
                             {"output", line.output}});
        }
        Json machines = Json::array();
        for (const script::ReplayMachineState& state : run.machines) {
            machines.push_back({{"at", static_cast<double>(state.at.count()) / 1000.0},
                                {"file", state.file},
                                {"line", state.line},
                                {"name", state.name},
                                {"state", state.state},
                                {"reason", state.reason}});
        }
        std::cout << Json{{"ok", run.ok},
                          {"lines", lines},
                          {"machines", machines},
                          {"ended", static_cast<double>(run.result.ended.count()) / 1000.0},
                          {"reachedLimit", run.result.reachedLimit}}
                         .dump(2)
                  << '\n';
        return run.ok ? kSuccess : kProblems;
    }
    for (const script::ReplayLine& line : run.lines) {
        std::cout << script::formatLine(line) << '\n';
    }
    std::cout << endText(run) << '\n';
    return run.ok ? kSuccess : kProblems;
}

int runTestCommand(const std::vector<std::wstring_view>& args)
{
    if (args.empty() || args.size() > 3) {
        return testUsage();
    }
    const fs::path folder(args[0]);
    bool update = false;
    bool json = false;
    for (std::size_t index = 1; index < args.size(); ++index) {
        if (args[index] == L"--update") {
            update = true;
        } else if (args[index] == L"--json") {
            json = true;
        } else {
            return testUsage();
        }
    }
    const std::vector<fs::path> files = script::testFiles(folder);
    if (files.empty()) {
        std::cerr << "gs test: no tests in " << utf8(folder / "tests") << " (files named <name>.test.json)\n";
        return kProblems;
    }
    const std::string entry = entryOf(folder).value_or(std::string());
    const script::ReplayKit kit = replayKit();
    Json results = Json::array();
    std::size_t passed = 0;
    for (const fs::path& file : files) {
        const script::TestOutcome outcome = script::runTest(file, folder, entry, update, kit);
        if (outcome.verdict == script::TestVerdict::Pass || outcome.verdict == script::TestVerdict::Updated) {
            ++passed;
        }
        if (json) {
            Json lines = Json::array();
            for (const script::ReplayLine& line : outcome.lines) {
                lines.push_back(script::formatLine(line));
            }
            results.push_back({{"name", outcome.name},
                               {"result", std::string(script::verdictName(outcome.verdict))},
                               {"detail", outcome.detail},
                               {"lines", lines}});
            continue;
        }
        std::cout << headingOf(outcome.verdict) << outcome.name
                  << (outcome.detail.empty() ? "" : ": " + outcome.detail) << '\n';
        if (outcome.verdict == script::TestVerdict::Fail) {
            for (const script::ReplayLine& line : outcome.lines) {
                std::cout << "    " << script::formatLine(line) << '\n';
            }
        }
    }
    const std::size_t failedCount = files.size() - passed;
    if (json) {
        std::cout << Json{{"ok", failedCount == 0}, {"tests", results}}.dump(2) << '\n';
    } else {
        std::cout << passed << " passed, " << failedCount << " failed\n";
    }
    return failedCount == 0 ? kSuccess : kProblems;
}

}
