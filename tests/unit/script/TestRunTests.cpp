#include "ScriptFixture.h"

#include "gs/script/Replay.h"
#include "gs/script/TestRun.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using gs::script::ControlInfo;
using gs::script::ReplayKit;
using gs::script::ReplayProfile;
using gs::script::ReplayRequest;
using gs::script::ReplayRun;
using gs::script::TestOutcome;
using gs::script::TestVerdict;
using gs::script::test::ScriptFolder;

namespace {

constexpr std::string_view kTriggerLogic = R"js(controls.jump.onPress(async () => {
    await controls.fire.trigger();
});
)js";

constexpr std::string_view kHoldLogic = R"js(controls.jump.onPress(async () => {
    await controls.fire.hold(100);
});
)js";

constexpr std::string_view kSession = R"json({
  "format": "gamescrcpy.session",
  "version": 1,
  "device": { "model": "Demo phone", "android": 15, "width": 1080, "height": 2400, "rotation": 0 },
  "startedAt": "2026-11-01T12:00:00Z",
  "events": [
    { "at": 100, "press": "jump" },
    { "at": 150, "release": "jump" }
  ]
}
)json";

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string readAll(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

ReplayKit kitWith(std::vector<ControlInfo> controls)
{
    return ReplayKit{.profile = [controls = std::move(controls)](
                                    const std::filesystem::path&) -> gs::Expected<ReplayProfile, std::string> {
        return ReplayProfile{.controls = controls};
    }};
}

ReplayKit demoKit()
{
    return kitWith({ControlInfo{"jump", "Jump", "hold", true, ""}, ControlInfo{"fire", "Fire", "hold", true, ""}});
}

void writeProfileFolder(const ScriptFolder& folder, std::string_view logic)
{
    folder.write("profile.json", "{}");
    folder.write("logic.js", logic);
    folder.write("sessions/basic.json", kSession);
}

}

TEST_CASE("SCR-017: replaySession plays a session through a folder's logic with the profile and reports what is missing")
{
    ScriptFolder folder;
    writeProfileFolder(folder, kTriggerLogic);
    const ReplayRun run = gs::script::replaySession(
        ReplayRequest{.folder = folder.path(), .entry = "logic.js", .session = folder.path() / "sessions" / "basic.json"},
        demoKit());
    CHECK(run.problem.empty());
    CHECK(run.ok);
    CHECK(gs::script::outputLines(run.lines)
          == std::vector<std::string>{"100 control fire down", "160 control fire up"});

    const ReplayRun noEntry = gs::script::replaySession(
        ReplayRequest{.folder = folder.path(), .session = folder.path() / "sessions" / "basic.json"}, demoKit());
    CHECK_FALSE(noEntry.ok);
    CHECK(noEntry.problem == "the profile folder has no logic.ts or logic.js");

    const ReplayKit broken{.profile = [](const std::filesystem::path&) -> gs::Expected<ReplayProfile, std::string> {
        return gs::makeUnexpected(std::string("$.controls: must be a list"));
    }};
    const ReplayRun badProfile = gs::script::replaySession(
        ReplayRequest{.folder = folder.path(), .entry = "logic.js", .session = folder.path() / "sessions" / "basic.json"},
        broken);
    CHECK(badProfile.problem == "profile " + utf8(folder.path() / "profile.json") + ": $.controls: must be a list");
}

TEST_CASE("SCR-017: a saved test passes on the same logic and fails at the first different line once the logic changes")
{
    ScriptFolder folder;
    writeProfileFolder(folder, kTriggerLogic);
    folder.write("tests/basic.test.json", R"({ "session": "../sessions/basic.json" })");
    const std::filesystem::path file = folder.path() / "tests" / "basic.test.json";

    const TestOutcome missingExpectation = gs::script::runTest(file, folder.path(), "logic.js", false, demoKit());
    CHECK(missingExpectation.verdict == TestVerdict::Error);
    CHECK(missingExpectation.detail == "$.expect must be a list of output lines; run gs test --update to write it");

    const TestOutcome updated = gs::script::runTest(file, folder.path(), "logic.js", true, demoKit());
    CHECK(updated.verdict == TestVerdict::Updated);
    CHECK(updated.name == "basic");
    const std::string written = readAll(file);
    CAPTURE(written);
    CHECK(written.find("\"100 control fire down\"") != std::string::npos);
    CHECK(written.find("\"160 control fire up\"") != std::string::npos);

    const TestOutcome passed = gs::script::runTest(file, folder.path(), "logic.js", false, demoKit());
    CHECK(passed.verdict == TestVerdict::Pass);
    CHECK(passed.detail.empty());
    CHECK(gs::script::verdictName(passed.verdict) == "pass");

    folder.write("logic.js", kHoldLogic);
    const TestOutcome failed = gs::script::runTest(file, folder.path(), "logic.js", false, demoKit());
    CHECK(failed.verdict == TestVerdict::Fail);
    CHECK_FALSE(failed.logicFailed);
    CHECK(failed.line == 2);
    CHECK(failed.expected == std::optional<std::string>("160 control fire up"));
    CHECK(failed.actual == std::optional<std::string>("200 control fire up"));
    CHECK(failed.detail == R"(output line 2: expected "160 control fire up", got "200 control fire up")");

    folder.write("logic.js", "controls.jump.onPress(() => { throw new Error(\"broken\"); });\n");
    const TestOutcome thrown = gs::script::runTest(file, folder.path(), "logic.js", false, demoKit());
    CHECK(thrown.verdict == TestVerdict::Fail);
    CHECK(thrown.logicFailed);
    CHECK(thrown.detail == "the script failed while replaying");
}

TEST_CASE("SCR-017: a test can name its recording with an absolute path, and reports a recording that is no longer there")
{
    ScriptFolder folder;
    writeProfileFolder(folder, kTriggerLogic);
    const std::filesystem::path session = folder.path() / "sessions" / "basic.json";
    folder.write("tests/absolute.test.json",
                 gs::script::testText(utf8(session), {"100 control fire down", "160 control fire up"}));
    folder.write("tests/relative.test.json", R"({ "session": "../sessions/basic.json", "expect": [] })");
    folder.write("tests/notes.txt", "not a test");

    const std::vector<std::filesystem::path> files = gs::script::testFiles(folder.path());
    REQUIRE(files.size() == 2);
    CHECK(files[0].filename() == "absolute.test.json");
    CHECK(files[1].filename() == "relative.test.json");

    const TestOutcome absolute = gs::script::runTest(files[0], folder.path(), "logic.js", false, demoKit());
    CHECK(absolute.verdict == TestVerdict::Pass);
    const TestOutcome shorter = gs::script::runTest(files[1], folder.path(), "logic.js", false, demoKit());
    CHECK(shorter.verdict == TestVerdict::Fail);
    CHECK(shorter.line == 1);
    CHECK_FALSE(shorter.expected.has_value());
    CHECK(shorter.actual == std::optional<std::string>("100 control fire down"));

    std::error_code error;
    std::filesystem::remove(session, error);
    REQUIRE_FALSE(error);
    const TestOutcome missing = gs::script::runTest(files[0], folder.path(), "logic.js", false, demoKit());
    CHECK(missing.verdict == TestVerdict::Error);
    CHECK(missing.missingRecording);
    CHECK(missing.detail == "the recording is no longer there: " + utf8(session));
    CHECK(gs::script::verdictName(missing.verdict) == "error");
}

TEST_CASE("SCR-017: the test file written for a recording holds its session path and the expected output lines")
{
    CHECK(gs::script::testText("D:/Videos/GameScrcpy/Pixel 2026-11-07 10.20.30.gsrec", {"300 tap 0.5 0.3 60"})
          == "{\n  \"session\": \"D:/Videos/GameScrcpy/Pixel 2026-11-07 10.20.30.gsrec\",\n  \"expect\": [\n"
             "    \"300 tap 0.5 0.3 60\"\n  ]\n}\n");
    ScriptFolder folder;
    folder.write("recording.gsrec/session.json", kSession);
    CHECK(gs::script::sessionFileOf(folder.path() / "recording.gsrec") == folder.path() / "recording.gsrec" / "session.json");
    CHECK(gs::script::sessionFileOf(folder.path() / "other.json") == folder.path() / "other.json");
}
