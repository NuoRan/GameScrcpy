#include "RecordingConvert.h"
#include "TaskLibrary.h"

#include <gs/base/Clock.h>
#include <gs/input/Profile.h>
#include <gs/script/ApiFiles.h>
#include <gs/script/Replay.h>
#include <gs/script/Report.h>
#include <gs/script/ScriptHost.h>
#include <gs/script/Services.h>
#include <gs/script/Session.h>
#include <gs/script/TestRun.h>

#include <doctest/doctest.h>

#include <QDir>
#include <QString>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using gs::app::LogicTests;
using gs::app::TaskEntry;
using gs::app::TaskLibrary;

namespace {

namespace fs = std::filesystem;
namespace script = gs::script;

constexpr std::string_view kProfile = R"json({
  "format": "gamescrcpy.profile",
  "version": 3,
  "meta": { "name": "Shooter" },
  "controls": [
    { "id": "fire", "name": "Fire", "type": "hold", "anchor": "center", "offset": [0.3, 0.2],
      "inputs": { "press": "KeyF" } },
    { "id": "dash-move", "name": "Dash", "type": "tap", "anchor": "center", "offset": [-0.3, 0.2],
      "inputs": { "press": "KeyQ" } },
    { "id": "turn", "name": "Turn", "type": "component", "component": "examples/in-turn", "anchor": "center",
      "offset": [0.1, -0.2], "inputs": { "key": "KeyT" } }
  ]
}
)json";

constexpr std::string_view kSession = R"json({
  "format": "gamescrcpy.session",
  "version": 2,
  "device": { "model": "Test phone", "android": 15, "width": 1000, "height": 500, "rotation": 1 },
  "startedAt": "2026-11-07T10:00:00Z",
  "frameRate": 60,
  "durationMs": 5000,
  "events": [
    { "at": 304, "press": "fire" },
    { "at": 397, "release": "fire" },
    { "at": 1000, "press": "dash-move" },
    { "at": 1050, "release": "dash-move" },
    { "at": 1100, "state": "aiming", "value": true },
    { "at": 1800, "press": "turn" },
    { "at": 1850, "release": "turn" },
    { "at": 2000, "press": "ghost" },
    { "at": 2100, "release": "ghost" },
    { "at": 2500, "press": "gone" },
    { "at": 2600, "release": "gone" }
  ],
  "operations": [
    { "at": 304, "touch": { "points": [{ "id": 0, "action": "down", "x": 0.65, "y": 0.7 }] } },
    { "at": 397, "touch": { "points": [{ "id": 0, "action": "up", "x": 0.65, "y": 0.7 }] } },
    { "at": 600, "touch": { "points": [{ "id": 2, "action": "down", "x": 0.5, "y": 0.3 }] } },
    { "at": 652, "touch": { "points": [{ "id": 2, "action": "up", "x": 0.5012, "y": 0.3 }] } },
    { "at": 1000, "touch": { "points": [{ "id": 1, "action": "down", "x": 0.35, "y": 0.7 }] } },
    { "at": 1060, "touch": { "points": [{ "id": 1, "action": "up", "x": 0.35, "y": 0.7 }] } },
    { "at": 1200, "touch": { "points": [{ "id": 24, "action": "down", "x": 0.9, "y": 0.9 }] } },
    { "at": 1260, "touch": { "points": [{ "id": 24, "action": "up", "x": 0.9, "y": 0.9 }] } },
    { "at": 1500, "touch": { "points": [{ "id": 3, "action": "down", "x": 0.2, "y": 0.5 }] } },
    { "at": 1600, "touch": { "points": [{ "id": 3, "action": "move", "x": 0.5, "y": 0.5 }] } },
    { "at": 1700, "touch": { "points": [{ "id": 3, "action": "up", "x": 0.8, "y": 0.5 }] } },
    { "at": 2000, "touch": { "points": [{ "id": 0, "action": "down", "x": 0.2, "y": 0.2 }] } },
    { "at": 2100, "touch": { "points": [{ "id": 0, "action": "up", "x": 0.2, "y": 0.2 }] } },
    { "at": 2200, "touch": { "points": [{ "id": 4, "action": "down", "x": 0.65, "y": 0.7 }] } },
    { "at": 2300, "touch": { "points": [{ "id": 4, "action": "move", "x": 0.75, "y": 0.7 }] } },
    { "at": 2350, "touch": { "points": [{ "id": 4, "action": "up", "x": 0.75, "y": 0.7 }] } },
    { "at": 3000, "text": { "length": 5 } },
    { "at": 3200, "key": { "action": "down", "keycode": 4, "metaState": 0, "repeat": 0 } },
    { "at": 3250, "key": { "action": "up", "keycode": 4, "metaState": 0, "repeat": 0 } },
    { "at": 3300, "scroll": { "x": 0.5, "y": 0.5, "h": 0, "v": -1 } },
    { "at": 3400, "scroll": { "x": 0.5, "y": 0.5, "h": 0, "v": -1 } },
    { "at": 3500, "touch": { "points": [{ "id": 0, "action": "down", "x": 0.4, "y": 0.6 }] } },
    { "at": 4000, "touch": { "points": [{ "id": 0, "action": "up", "x": 0.4, "y": 0.6 }] } }
  ]
}
)json";

constexpr std::string_view kExpectedTask = R"ts(export default class RecordedActions extends Task {
    async run() {
        await sleep(300);
        await controls.fire.hold(100);
        await sleep(200);
        await touch.tap(0.5, 0.3);
        await sleep(340);
        await controls["dash-move"].hold(50);
        await sleep(450);
        log.info("A swipe was here; swipes are not turned into steps");
        await sleep(300);
        log.info("Turn was pressed here; components made with logic are not turned into steps");
        await sleep(200);
        await touch.tap(0.2, 0.2);
        await sleep(440);
        log.info("The component gone was pressed here, but the profile no longer has it");
        await sleep(500);
        log.info("Typed 5 character(s) here; typing is not turned into steps");
        await sleep(200);
        log.info("The Android key 4 was sent here; keys are not turned into steps");
        await sleep(100);
        log.info("A scroll was here; scrolling is not turned into steps");
        await sleep(200);
        await touch.tap(0.4, 0.6, 500);
    }
}
)ts";

constexpr std::string_view kTapLogic = R"ts(controls.fire.onPress(async () => {
    await touch.tap(0.1, 0.1);
});
)ts";

constexpr std::string_view kMovedTapLogic = R"ts(controls.fire.onPress(async () => {
    await touch.tap(0.2, 0.2);
});
)ts";

fs::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

QString textOf(const fs::path& path)
{
    return QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
}

void writeText(const fs::path& file, std::string_view text)
{
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream out(file, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string readText(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

gs::input::Profile shooter()
{
    const gs::input::LoadResult loaded = gs::input::loadProfile(kProfile);
    REQUIRE(loaded.ok());
    return loaded.profile;
}

script::Session sessionOf(std::string_view text)
{
    const auto session = script::parseSession(text);
    REQUIRE(session.has_value());
    return *session;
}

bool failing(script::ReportKind kind)
{
    return kind == script::ReportKind::ScriptFailed || kind == script::ReportKind::TaskFailed ||
           kind == script::ReportKind::TaskInterrupted || kind == script::ReportKind::UnhandledRejection;
}

std::vector<std::string> replayTask(const QString& code, const gs::input::Profile& profile,
                                    const script::DeviceInfo& device)
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const fs::path folder = pathOf(root.path());
    writeText(folder / "task.ts", code.toStdString());
    gs::ManualClock clock;
    const script::ReplayProfile replayed = gs::app::replayProfileOf(profile);
    script::RecordingServices services(clock, device, replayed.controls, replayed.states);
    std::vector<std::string> failures;
    const script::ScriptHost::Sink sink = [&failures](const script::Report& report) {
        if (failing(report.kind)) {
            failures.push_back(script::describe(report));
        }
    };
    script::ReplayOptions options;
    options.source = script::ScriptSource{.folder = folder, .entry = "task.ts"};
    options.events = {script::SessionEvent{gs::Microseconds{0}, script::SessionEventKind::Run, "default", false}};
    options.until = gs::Microseconds{10'000'000};
    const script::ReplayResult result = script::replay(options, services, clock, sink);
    CHECK(result.loaded);
    CHECK(failures.empty());
    CHECK_FALSE(result.reachedLimit);
    return script::outputLines(services.lines());
}

}

TEST_CASE("SCR-017: a recording turns into a task that waits, holds the recorded components, taps where it tapped and "
          "notes what it skips")
{
    const gs::input::Profile profile = shooter();
    const std::optional<QString> code = gs::app::taskCodeFor(sessionOf(kSession), &profile);
    REQUIRE(code.has_value());
    const std::string text = code->toStdString();
    CAPTURE(text);
    CHECK(text == kExpectedTask);
}

TEST_CASE("SCR-017: the task made from a recording replays the recording's control presses and taps at the same "
          "times")
{
    const gs::input::Profile profile = shooter();
    const script::Session session = sessionOf(kSession);
    const std::optional<QString> code = gs::app::taskCodeFor(session, &profile);
    REQUIRE(code.has_value());
    const std::vector<std::string> lines = replayTask(*code, profile, session.device);
    CHECK(lines == std::vector<std::string>{"300 control fire down", "400 control fire up", "600 tap 0.5 0.3 60",
                                            "1000 control dash-move down", "1050 control dash-move up",
                                            "2000 tap 0.2 0.2 60", "3500 tap 0.4 0.6 500"});
}

TEST_CASE("SCR-017: a hold that overlaps later steps starts without waiting, the task waits for it at the end, and a "
          "hold longer than a minute is split")
{
    const gs::input::Profile profile = shooter();
    const script::Session overlapping = sessionOf(R"json({
  "format": "gamescrcpy.session", "version": 2,
  "device": { "model": "Test phone", "android": 15, "width": 1000, "height": 500, "rotation": 1 },
  "durationMs": 4000,
  "events": [ { "at": 0, "press": "fire" }, { "at": 3000, "release": "fire" } ],
  "operations": [
    { "at": 0, "touch": { "points": [{ "id": 0, "action": "down", "x": 0.65, "y": 0.7 }] } },
    { "at": 1000, "touch": { "points": [{ "id": 1, "action": "down", "x": 0.5, "y": 0.5 }] } },
    { "at": 1050, "touch": { "points": [{ "id": 1, "action": "up", "x": 0.5, "y": 0.5 }] } },
    { "at": 3000, "touch": { "points": [{ "id": 0, "action": "up", "x": 0.65, "y": 0.7 }] } }
  ]
}
)json");
    const std::optional<QString> overlap = gs::app::taskCodeFor(overlapping, &profile);
    REQUIRE(overlap.has_value());
    CHECK(overlap->toStdString() == "export default class RecordedActions extends Task {\n"
                                    "    async run() {\n"
                                    "        controls.fire.hold(3000);\n"
                                    "        await sleep(1000);\n"
                                    "        await touch.tap(0.5, 0.5);\n"
                                    "        await sleep(1940);\n"
                                    "    }\n"
                                    "}\n");
    CHECK(replayTask(*overlap, profile, overlapping.device)
          == std::vector<std::string>{"0 control fire down", "1000 tap 0.5 0.5 60", "3000 control fire up"});

    const script::Session longHold = sessionOf(R"json({
  "format": "gamescrcpy.session", "version": 2,
  "device": { "model": "Test phone", "android": 15, "width": 1000, "height": 500, "rotation": 1 },
  "durationMs": 80000,
  "events": [ { "at": 0, "press": "fire" }, { "at": 70000, "release": "fire" } ]
}
)json");
    const std::optional<QString> split = gs::app::taskCodeFor(longHold, &profile);
    REQUIRE(split.has_value());
    CHECK(split->toStdString() == "export default class RecordedActions extends Task {\n"
                                  "    async run() {\n"
                                  "        await controls.fire.hold(60000);\n"
                                  "        await controls.fire.hold(10000);\n"
                                  "    }\n"
                                  "}\n");

    const script::Session empty = sessionOf(R"json({ "format": "gamescrcpy.session", "version": 2,
  "events": [ { "at": 100, "state": "aiming", "value": true } ] })json");
    CHECK_FALSE(gs::app::taskCodeFor(empty, &profile).has_value());
}

TEST_CASE("SCR-017: a finger a stick or a view puts down without a key press is neither a tap nor a swipe")
{
    const gs::input::LoadResult loaded = gs::input::loadProfile(R"json({"format": "gamescrcpy.profile", "version": 3,
  "controls": [ { "id": "move", "name": "Move", "type": "wasd", "anchor": "center", "offset": [-0.6, 0],
                  "inputs": { "up": "KeyW" } } ] })json");
    REQUIRE(loaded.ok());
    const script::Session session = sessionOf(R"json({
  "format": "gamescrcpy.session", "version": 2,
  "device": { "model": "Test phone", "android": 15, "width": 1000, "height": 500, "rotation": 1 },
  "durationMs": 1000,
  "operations": [
    { "at": 100, "touch": { "points": [{ "id": 0, "action": "down", "x": 0.2, "y": 0.5 }] } },
    { "at": 200, "touch": { "points": [{ "id": 0, "action": "move", "x": 0.201, "y": 0.5 }] } },
    { "at": 300, "touch": { "points": [{ "id": 0, "action": "up", "x": 0.201, "y": 0.5 }] } },
    { "at": 500, "touch": { "points": [{ "id": 1, "action": "down", "x": 0.6, "y": 0.4 }] } },
    { "at": 560, "touch": { "points": [{ "id": 1, "action": "up", "x": 0.6, "y": 0.4 }] } }
  ]
}
)json");
    const std::optional<QString> code = gs::app::taskCodeFor(session, &loaded.profile);
    REQUIRE(code.has_value());
    CHECK(code->toStdString() == "export default class RecordedActions extends Task {\n"
                                 "    async run() {\n"
                                 "        await sleep(500);\n"
                                 "        await touch.tap(0.6, 0.4);\n"
                                 "    }\n"
                                 "}\n");
}

TEST_CASE("SCR-017: without the profile, recorded component presses become taps where they touched, or notes")
{
    const std::optional<QString> code = gs::app::taskCodeFor(sessionOf(kSession), nullptr);
    REQUIRE(code.has_value());
    const std::string text = code->toStdString();
    CAPTURE(text);
    CHECK(text.find("controls.") == std::string::npos);
    CHECK(text.find("        await touch.tap(0.65, 0.7);\n") != std::string::npos);
    CHECK(text.find("        await touch.tap(0.35, 0.7);\n") != std::string::npos);
    CHECK(text.find("log.info(\"The component turn was pressed here, but the profile no longer has it\");")
          != std::string::npos);
}

TEST_CASE("SCR-017: Turn into blueprint saves a task named after the recording, with the profile's components typed")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const fs::path recording = pathOf(root.path()) / "recordings" / "Pixel 7 2026-11-07 10.20.30.gsrec";
    writeText(recording / "session.json", kSession);
    TaskLibrary library(pathOf(root.path()) / "tasks");
    const gs::input::Profile profile = shooter();
    CHECK(gs::app::recordingTitle(textOf(recording)) == QStringLiteral("Pixel 7 2026-11-07 10.20.30"));
    CHECK(gs::app::recordingTitle(QStringLiteral("D:/Videos/My run")) == QStringLiteral("My run"));

    const auto made = gs::app::turnRecordingIntoTask(library, textOf(recording), &profile);
    REQUIRE(made.has_value());
    CHECK(made->name == QStringLiteral("Pixel 7 2026-11-07 10.20.30 — actions"));
    const TaskEntry* entry = library.find(made->id);
    REQUIRE(entry != nullptr);
    CHECK(entry->name == made->name);
    CHECK(entry->schedule == QStringLiteral("manual"));
    const fs::path folder = pathOf(entry->folder);
    CHECK(readText(folder / "task.ts") == kExpectedTask);
    CHECK(readText(folder / "tsconfig.json") == script::profileTsconfig());
    const std::string types = readText(folder / "profile.d.ts");
    CAPTURE(types);
    CHECK(types.find("readonly fire: Hold & { readonly position: PicturePoint };") != std::string::npos);
    CHECK(types.find("readonly \"dash-move\": Tap & { readonly position: PicturePoint };") != std::string::npos);

    const auto again = gs::app::turnRecordingIntoTask(library, textOf(recording), &profile);
    REQUIRE(again.has_value());
    CHECK(again->id != made->id);
    CHECK(again->name == QStringLiteral("Pixel 7 2026-11-07 10.20.30 — actions 2"));
    CHECK(library.tasks().size() == 2);

    const auto bare = gs::app::turnRecordingIntoTask(library, textOf(recording), nullptr);
    REQUIRE(bare.has_value());
    const TaskEntry* untyped = library.find(bare->id);
    REQUIRE(untyped != nullptr);
    CHECK_FALSE(fs::exists(pathOf(untyped->folder) / "profile.d.ts"));

    const auto missing = gs::app::turnRecordingIntoTask(library, textOf(pathOf(root.path()) / "nothing.gsrec"), &profile);
    CHECK_FALSE(missing.has_value());
    CHECK(library.tasks().size() == 3);
}

TEST_CASE("SCR-017: Save as test keeps the logic's output on the recording, and Run tests passes, then shows the first "
          "different line, then reports the recording is gone")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const fs::path recording = pathOf(root.path()) / "recordings" / "Pixel 7 2026-11-07 10.20.30.gsrec";
    writeText(recording / "session.json", kSession);
    const fs::path profileFolder = pathOf(root.path()) / "profiles" / "shooter";
    writeText(profileFolder / "profile.json", kProfile);
    writeText(profileFolder / "logic.ts", kTapLogic);

    const auto saved = gs::app::saveRecordingTest(textOf(profileFolder), textOf(recording));
    REQUIRE(saved.has_value());
    CHECK(*saved == QStringLiteral("Pixel 7 2026-11-07 10.20.30"));
    const fs::path file = profileFolder / "tests" / "Pixel 7 2026-11-07 10.20.30.test.json";
    REQUIRE(fs::is_regular_file(file));
    const std::string written = readText(file);
    CAPTURE(written);
    CHECK(written == script::testText(textOf(recording).toStdString(), {"304 tap 0.1 0.1 60"}));

    const std::vector<script::TestOutcome> passed = gs::app::runLogicTests(textOf(profileFolder));
    REQUIRE(passed.size() == 1);
    CHECK(passed[0].verdict == script::TestVerdict::Pass);
    const QVariantMap shownPass = gs::app::shownTestResult(passed[0]);
    CHECK(shownPass.value(QStringLiteral("name")).toString() == QStringLiteral("Pixel 7 2026-11-07 10.20.30"));
    CHECK(shownPass.value(QStringLiteral("result")).toString() == QStringLiteral("pass"));
    CHECK(shownPass.value(QStringLiteral("text")).toString().isEmpty());

    writeText(profileFolder / "logic.ts", kMovedTapLogic);
    const std::vector<script::TestOutcome> failed = gs::app::runLogicTests(textOf(profileFolder));
    REQUIRE(failed.size() == 1);
    CHECK(failed[0].verdict == script::TestVerdict::Fail);
    const QVariantMap shownFail = gs::app::shownTestResult(failed[0]);
    CHECK(shownFail.value(QStringLiteral("result")).toString() == QStringLiteral("fail"));
    CHECK(shownFail.value(QStringLiteral("text")).toString()
          == QStringLiteral("Line 1: expected “304 tap 0.1 0.1 60”, got “304 tap 0.2 0.2 60”"));

    LogicTests tests;
    tests.show(failed);
    CHECK_FALSE(tests.running());
    CHECK(tests.results().size() == 1);
    CHECK(tests.note() == QStringLiteral("1 of 1 test(s) did not pass"));

    std::error_code error;
    fs::remove_all(recording, error);
    REQUIRE_FALSE(error);
    const std::vector<script::TestOutcome> gone = gs::app::runLogicTests(textOf(profileFolder));
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].verdict == script::TestVerdict::Error);
    CHECK(gone[0].missingRecording);
    const QVariantMap shownGone = gs::app::shownTestResult(gone[0]);
    CHECK(shownGone.value(QStringLiteral("result")).toString() == QStringLiteral("error"));
    CHECK(shownGone.value(QStringLiteral("text")).toString() == QStringLiteral("The recording is no longer there"));

    tests.show(passed);
    CHECK(tests.note() == QStringLiteral("All 1 test(s) passed"));
    tests.show({});
    CHECK(tests.note() == QStringLiteral("There are no tests in the tests folder"));
}

TEST_CASE("SCR-017: Save as test refuses when the logic fails on the recording or the profile has no logic")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const fs::path recording = pathOf(root.path()) / "Pixel.gsrec";
    writeText(recording / "session.json", kSession);
    const fs::path profileFolder = pathOf(root.path()) / "shooter";
    writeText(profileFolder / "profile.json", kProfile);

    CHECK_FALSE(gs::app::saveRecordingTest(textOf(profileFolder), textOf(recording)).has_value());
    writeText(profileFolder / "logic.ts", "controls.fire.onPress(() => { throw new Error(\"broken\"); });\n");
    const auto refused = gs::app::saveRecordingTest(textOf(profileFolder), textOf(recording));
    CHECK_FALSE(refused.has_value());
    CHECK_FALSE(fs::exists(profileFolder / "tests" / "Pixel.test.json"));
}
