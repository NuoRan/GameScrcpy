#include "ScriptFixture.h"

#include "gs/script/ApiFiles.h"
#include "gs/script/Replay.h"
#include "gs/script/Resources.h"
#include "gs/script/Session.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using gs::ManualClock;
using gs::Microseconds;
using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::formatLine;
using gs::script::RecordingServices;
using gs::script::replay;
using gs::script::ReplayMachineState;
using gs::script::ReplayOptions;
using gs::script::ReplayResult;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::SessionEvent;
using gs::script::SessionEventKind;
using gs::script::StateInfo;
using gs::script::test::ScriptFolder;

namespace {

struct Replayed {
    std::vector<std::string> lines;
    ReplayResult result;
};

SessionEvent press(double ms, std::string id)
{
    return {Microseconds{static_cast<std::int64_t>(ms * 1000)}, SessionEventKind::Press, std::move(id), true};
}

SessionEvent release(double ms, std::string id)
{
    return {Microseconds{static_cast<std::int64_t>(ms * 1000)}, SessionEventKind::Release, std::move(id), false};
}

SessionEvent run(double ms, std::string handler)
{
    return {Microseconds{static_cast<std::int64_t>(ms * 1000)}, SessionEventKind::Run, std::move(handler), false};
}

SessionEvent stateChange(double ms, std::string id, bool value)
{
    return {Microseconds{static_cast<std::int64_t>(ms * 1000)}, SessionEventKind::State, std::move(id), value};
}

Replayed replayIn(const ScriptFolder& folder, std::vector<SessionEvent> events, std::vector<ControlInfo> controls,
                  std::vector<StateInfo> states = {}, std::uint64_t seed = 1, double frameRate = 60.0)
{
    ManualClock clock;
    RecordingServices services(clock, DeviceInfo{"Pixel", 15, 1080, 2400, 0}, std::move(controls), std::move(states));
    const ScriptHost::Sink sink = [&services](const Report& report) {
        if (report.kind == ReportKind::Log) {
            services.note("log " + report.message);
        } else if (report.kind == ReportKind::ScriptFailed || report.kind == ReportKind::TaskFailed
                   || report.kind == ReportKind::UnhandledRejection) {
            services.note("error " + report.message);
        }
    };
    ReplayOptions options;
    options.source = folder.source();
    options.events = std::move(events);
    options.seed = seed;
    options.frameRate = frameRate;
    options.until = Microseconds{10'000'000};
    Replayed replayed;
    replayed.result = replay(options, services, clock, sink);
    for (const auto& line : services.lines()) {
        replayed.lines.push_back(formatLine(line));
    }
    return replayed;
}

Replayed replayScript(std::string_view main, std::vector<SessionEvent> events, std::vector<ControlInfo> controls,
                      std::vector<StateInfo> states = {}, std::uint64_t seed = 1, double frameRate = 60.0)
{
    ScriptFolder folder;
    folder.write("main.js", main);
    return replayIn(folder, std::move(events), std::move(controls), std::move(states), seed, frameRate);
}

std::vector<ControlInfo> controlsNamed(std::initializer_list<const char*> ids)
{
    std::vector<ControlInfo> controls;
    for (const char* id : ids) {
        controls.push_back({id, id, "tap", true});
    }
    return controls;
}

constexpr std::string_view kAgainScript = R"js(
for (const rule of ["ignore", "restart", "stop", "queue", "parallel"]) {
    controls[rule].onPress(async () => {
        log.info(rule, "start");
        await sleep(100);
        log.info(rule, "end");
    }, { again: rule });
}
)js";

std::vector<std::string> againLines(const char* rule)
{
    return replayScript(kAgainScript, {press(0, rule), release(10, rule), press(50, rule), release(60, rule)},
                        controlsNamed({"ignore", "restart", "stop", "queue", "parallel"}))
        .lines;
}

}

TEST_CASE("replay: time, dates and random numbers come from the host, so the same seed gives the same run")
{
    constexpr std::string_view script = R"js(
export function start() {
    log.info(Math.random(), Date.now(), new Date().toISOString(), time.now(), typeof Date(), new Date(0).getTime());
}
)js";
    const Replayed first = replayScript(script, {run(250, "start")}, {});
    const Replayed again = replayScript(script, {run(250, "start")}, {});
    const Replayed other = replayScript(script, {run(250, "start")}, {}, {}, 2);
    REQUIRE(first.result.loaded);
    REQUIRE(first.lines.size() == 1);
    CHECK(first.lines == again.lines);
    CHECK(first.lines != other.lines);
    CHECK(first.lines[0].starts_with("250 # log 0."));
    CHECK(first.lines[0].ends_with(" 1767225600250 2026-01-01T00:00:00.250Z 250 string 0"));
    CHECK(first.result.ended == Microseconds{250'000});
}

TEST_CASE("replay: the again rule decides what a second press does while the first task still runs")
{
    CHECK(againLines("ignore") == std::vector<std::string>{"0 # log ignore start", "100 # log ignore end"});
    CHECK(againLines("restart")
          == std::vector<std::string>{"0 # log restart start", "50 # log restart start", "150 # log restart end"});
    CHECK(againLines("stop") == std::vector<std::string>{"0 # log stop start"});
    CHECK(againLines("queue") == std::vector<std::string>{"0 # log queue start", "100 # log queue end",
                                                         "100 # log queue start", "200 # log queue end"});
    CHECK(againLines("parallel") == std::vector<std::string>{"0 # log parallel start", "50 # log parallel start",
                                                            "100 # log parallel end", "150 # log parallel end"});
}

TEST_CASE("replay: holding cancels on release, listeners made in a task end with it, releases have their own event")
{
    const Replayed held = replayScript(R"js(
controls.dash.whileHeld(async ({ signal }) => {
    while (!signal.aborted) {
        await touch.tap(0.5, 0.5, 30);
        await sleep(70);
    }
});
controls.jump.onRelease(({ control }) => log.info("released", control));
export async function listen() {
    controls.jump.onPress(() => log.info("pressed inside"));
    await sleep(100);
}
)js",
                                       {press(0, "dash"), release(250, "dash"), run(0, "listen"), press(50, "jump"),
                                        release(60, "jump"), press(150, "jump")},
                                       controlsNamed({"dash", "jump"}));
    CHECK(held.lines == std::vector<std::string>{"0 tap 0.5 0.5 30", "50 # log pressed inside", "60 # log released jump",
                                                 "100 tap 0.5 0.5 30", "200 tap 0.5 0.5 30"});
}

TEST_CASE("replay: state changes from the script and from the session both reach onChange, unchanged values do not")
{
    const Replayed changes = replayScript(R"js(
vars.aiming.onChange(({ id, value }) => log.info("changed", id, value, vars.aiming.value));
export function toggle() {
    vars.aiming.value = !vars.aiming.value;
}
export function same() {
    vars.aiming.value = vars.aiming.value;
}
)js",
                                          {run(0, "toggle"), stateChange(100, "aiming", false), run(200, "same"),
                                           run(300, "toggle")},
                                          {}, {{"aiming", "Aiming", false}});
    CHECK(changes.lines == std::vector<std::string>{"0 state aiming on", "0 # log changed aiming true true",
                                                    "100 # log changed aiming false false", "200 state aiming off",
                                                    "300 state aiming on", "300 # log changed aiming true true"});
}

TEST_CASE("replay: wrong listeners are refused with stable error codes")
{
    const Replayed wrong = replayScript(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
log.info(codeOf(() => controls.jump.onPress(42)), codeOf(() => controls.jump.onPress(() => {}, { again: "twice" })),
         Object.isFrozen(controls), Object.isFrozen(controls.jump), String(controls.jump), controls.missing === undefined);
)js",
                                        {}, controlsNamed({"jump"}));
    CHECK(wrong.lines == std::vector<std::string>{"0 # log GS-API-008 GS-API-009 true true [object Tap] true"});
}

SessionEvent answer(double ms, std::string json)
{
    return {Microseconds{static_cast<std::int64_t>(ms * 1000)}, SessionEventKind::Answer, std::move(json), false};
}

TEST_CASE("ui: questions are answered from the session in order, then by default, and a cancelled one is withdrawn (ADR-0082)")
{
    const Replayed answered = replayScript(R"js(
ui.toast("hello");
controls.jump.onPress(async () => {
    const sure = await ui.confirm("Sell?", { ok: "Sell", cancel: "Keep" });
    const mode = await ui.choose("Mode", ["Easy", "Hard"]);
    const team = await ui.ask("Team", { initial: "red" });
    const again = await ui.confirm("Again?");
    log.info(sure, mode, team, again, input.cursor(), input.isDown("KeyW"));
});
controls.fire.onPress(async () => {
    try {
        await ui.ask("Never answered");
    } catch (error) {
        log.info(error.name);
    }
});
)js",
                                           {press(0, "jump"), answer(10, "true"), answer(20, "1"),
                                            answer(30, "\"blue\"")},
                                           controlsNamed({"jump", "fire"}));
    CHECK(answered.lines == std::vector<std::string>{
                                "0 toast hello", "0 ask confirm Sell?", "10 answer true", "10 ask choose Mode | Easy | Hard",
                                "20 answer 1", "20 ask ask Team", "30 answer \"blue\"", "30 ask confirm Again?",
                                "30 answer false", "30 # log true 1 blue false null false"});

    const Replayed cancelled = replayScript(R"js(
controls.fire.whileHeld(async () => {
    try {
        await ui.ask("Name?");
    } catch (error) {
        log.info(error.name);
    }
});
)js",
                                            {press(0, "fire"), release(5, "fire"), answer(50, "\"late\"")},
                                            controlsNamed({"fire"}));
    CHECK(cancelled.lines ==
          std::vector<std::string>{"0 ask ask Name?", "5 ask withdrawn", "5 # log AbortError"});

    const Replayed wrong = replayScript(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
log.info(codeOf(() => ui.toast(1)), codeOf(() => ui.choose("Pick", ["only"])), codeOf(() => input.isDown(3)));
)js",
                                        {}, controlsNamed({"jump"}));
    CHECK(wrong.lines == std::vector<std::string>{"0 # log GS-API-011 GS-API-012 GS-API-013"});
}

TEST_CASE("ui: drawings last until disposed, their duration ends or the task that drew them ends (ADR-0082)")
{
    const Replayed drawn = replayScript(R"js(
const top = ui.box(0.1, 0.1, 0.2, 0.2, { color: "good", label: "chest" });
ui.text(0.2, 0.2, "brief", { durationMs: 100 });
controls.jump.onPress(() => {
    ui.text(0.5, 0.5, "inside");
    ui.box(0, 0, 0.5, 0.5, { durationMs: 1000 });
});
controls.fire.onPress(() => {
    top.dispose();
    top.dispose();
});
)js",
                                        {press(50, "jump"), press(200, "fire")}, controlsNamed({"jump", "fire"}));
    CHECK(drawn.lines == std::vector<std::string>{"0 draw 1 box 0.1 0.1 0.2 0.2 good chest",
                                                  "0 draw 2 text 0.2 0.2 accent brief",
                                                  "50 draw 3 text 0.5 0.5 accent inside",
                                                  "50 draw 4 box 0 0 0.5 0.5 accent",
                                                  "50 erase 3",
                                                  "50 erase 4",
                                                  "100 erase 2",
                                                  "200 erase 1"});

    const Replayed limited = replayScript(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
for (let i = 0; i < 64; i += 1) {
    ui.text(0, 0, "x");
}
log.info(codeOf(() => ui.text(0, 0, "y")), codeOf(() => ui.box(0.9, 0.9, 0.2, 0.2)),
         codeOf(() => ui.text(0, 0, "z", { color: "pink" })), codeOf(() => ui.box(0, 0, 0.1, 0.1, { durationMs: 0 })));
)js",
                                          {}, controlsNamed({"jump"}));
    REQUIRE_FALSE(limited.lines.empty());
    CHECK(std::ranges::count_if(limited.lines, [](const std::string& line) { return line.starts_with("0 draw "); }) ==
          64);
    CHECK(limited.lines.back() == "0 # log GS-API-010 GS-API-002 GS-API-014 GS-API-003");
}

TEST_CASE("vision: a replay has no picture yet, so find gives null and findAll nothing, and bad arguments throw (ADR-0083)")
{
    const Replayed found = replayScript(R"js(
controls.jump.onPress(async () => {
    const one = await vision.find("res/chest.png");
    const all = await vision.findAll("res/coin.png", {
        max: 8, threshold: 0.9, timeoutMs: 100, region: { x: 0, y: 0.5, width: 1, height: 0.5 },
    });
    log.info(one, all.length);
});
)js",
                                        {press(10, "jump")}, controlsNamed({"jump"}));
    CHECK(found.lines == std::vector<std::string>{"10 vision find res/chest.png", "10 vision findAll res/coin.png",
                                                  "10 # log null 0"});

    const Replayed wrong = replayScript(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
log.info(codeOf(() => vision.find("/top.png")), codeOf(() => vision.find("res/../up.png")),
         codeOf(() => vision.find("res/x.gif")), codeOf(() => vision.find("C:/x.png")),
         codeOf(() => vision.find("res/x.png", { threshold: 2 })), codeOf(() => vision.findAll("res/x.png", { max: 0 })),
         codeOf(() => vision.findAll("res/x.png", { max: 1.5 })), codeOf(() => vision.find("res/x.png", { timeoutMs: 20000 })),
         codeOf(() => vision.find("res/x.png", { region: { x: 0.5, y: 0, width: 0.6, height: 1 } })),
         codeOf(() => vision.find("res/x.png", { region: { x: 0.5, y: 0 } })), codeOf(() => vision.find("res/x.png", 3)));
)js",
                                        {}, controlsNamed({"jump"}));
    CHECK(wrong.lines == std::vector<std::string>{"0 # log GS-API-015 GS-API-015 GS-API-015 GS-API-015 GS-API-017 "
                                                  "GS-API-017 GS-API-017 GS-API-003 GS-API-002 GS-API-002 GS-API-011"});
}

TEST_CASE("clipboard: a replay records reads and writes and has no phone clipboard, so a read gives null and a write "
          "false (ADR-0091)")
{
    const Replayed clipped = replayScript(R"js(
controls.jump.onPress(async () => {
    const text = await clipboard.read();
    const sent = await clipboard.write("hello 你好");
    log.info(text, sent);
});
)js",
                                          {press(10, "jump")}, controlsNamed({"jump"}));
    CHECK(clipped.lines == std::vector<std::string>{"10 clipboard read", "10 clipboard write hello 你好",
                                                    "10 # log null false"});
}

TEST_CASE("vision: waitFor keeps looking until its time is up, waitGone and findAndTap look once, watch looks until "
          "disposed (ADR-0091)")
{
    const Replayed looked = replayScript(R"js(
controls.jump.onPress(async () => {
    log.info("for", await vision.waitFor("res/chest.png", { timeoutMs: 1000, intervalMs: 250 }));
    log.info("gone", await vision.waitGone("res/chest.png"));
    log.info("tap", await vision.findAndTap("res/chest.png"));
});
const watching = vision.watch("res/boss.png", () => log.info("appeared"), () => log.info("left"), { intervalMs: 2000 });
after(5000, () => watching.dispose());
)js",
                                         {press(10, "jump")}, controlsNamed({"jump"}));
    const auto at = [&looked](std::string_view line) {
        std::vector<std::string> times;
        for (const std::string& each : looked.lines) {
            if (each.ends_with(line)) {
                times.push_back(each.substr(0, each.find(' ')));
            }
        }
        return times;
    };
    CHECK(at(" vision find res/chest.png") ==
          std::vector<std::string>{"10", "260", "510", "760", "1010", "1010", "1010"});
    CHECK(at(" # log for null") == std::vector<std::string>{"1010"});
    CHECK(at(" # log gone true") == std::vector<std::string>{"1010"});
    CHECK(at(" # log tap null") == std::vector<std::string>{"1010"});
    CHECK(at(" vision find res/boss.png") == std::vector<std::string>{"0", "2000", "4000"});
    CHECK(at(" # log appeared").empty());
    CHECK(at(" # log left").empty());

    const Replayed wrong = replayScript(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
log.info(codeOf(() => vision.waitFor("res/x.gif")), codeOf(() => vision.waitFor("res/x.png", { timeoutMs: 0 })),
         codeOf(() => vision.waitGone("res/x.png", { intervalMs: 10 })), codeOf(() => vision.findAndTap("C:/x.png")),
         codeOf(() => vision.watch("res/x.png", 3)), codeOf(() => vision.watch("res/x.png", () => {}, null, { again: "no" })),
         codeOf(() => vision.watch("res/x.png", () => {}, undefined, { intervalMs: 70000 })));
)js",
                                        {}, controlsNamed({"jump"}));
    CHECK(wrong.lines == std::vector<std::string>{"0 # log GS-API-015 GS-API-003 GS-API-003 GS-API-015 GS-API-008 "
                                                  "GS-API-009 GS-API-003"});
}

TEST_CASE("vision: a replay has no picture, so pickColor gives null and matchColor false, and marks are recorded "
          "(ADR-0091)")
{
    const Replayed seen = replayScript(R"js(
ui.mark(0.5, 0.5, { durationMs: 100 });
controls.jump.onPress(async () => {
    const color = await vision.pickColor(point(0.25, 0.5));
    const same = await vision.matchColor({ x: 0.5, y: 0.75 }, "#F5C542", { tolerance: 0.2 });
    ui.mark(0.25, 0.5, { color: "good", label: "probe" });
    log.info("pixel", color, same);
});
)js",
                                       {press(200, "jump")}, controlsNamed({"jump"}));
    CHECK(seen.lines == std::vector<std::string>{"0 draw 1 mark 0.5 0.5 accent", "100 erase 1",
                                                 "200 vision pickColor 0.25 0.5",
                                                 "200 vision matchColor 0.5 0.75 #F5C542",
                                                 "200 draw 2 mark 0.25 0.5 good probe", "200 # log pixel null false",
                                                 "200 erase 2"});
}

TEST_CASE("sessions: the format, events in time order, and problems named by their path")
{
    const auto session = gs::script::parseSession(R"({"format": "gamescrcpy.session", "version": 1,
        "startedAt": "2026-11-01T12:00:00.5Z", "frameRate": 30,
        "events": [{"at": 20, "press": "a"}, {"at": 5.5, "state": "s", "value": true}, {"at": 10, "run": "go"}]})");
    REQUIRE(session.has_value());
    CHECK(session->startedAtMs == 1'793'534'400'500);
    CHECK(session->frameRate == 30.0);
    CHECK(session->device.width == 1080);
    REQUIRE(session->events.size() == 3);
    CHECK(session->events[0] == SessionEvent{Microseconds{5500}, SessionEventKind::State, "s", true});
    CHECK(session->events[1].kind == SessionEventKind::Run);
    CHECK(session->events[2].kind == SessionEventKind::Press);

    const struct {
        const char* text;
        const char* problem;
    } broken[] = {
        {R"({"format": "other", "version": 1})", "$.format:"},
        {R"({"format": "gamescrcpy.session", "version": 3})", "$.version:"},
        {R"({"format": "gamescrcpy.session", "version": 1, "events": [{"at": 1, "press": "a", "release": "a"}]})",
         "$.events[0]: needs exactly one"},
        {R"({"format": "gamescrcpy.session", "version": 1, "events": [{"at": 1, "state": "s"}]})", "$.events[0].value:"},
        {R"({"format": "gamescrcpy.session", "version": 1, "events": [{"at": -1, "run": "go"}]})", "$.events[0].at:"},
        {R"({"format": "gamescrcpy.session", "version": 1, "startedAt": "yesterday"})", "$.startedAt:"},
        {R"({"format": "gamescrcpy.session", "version": 1, "device": {"width": 0}})", "$.device.width:"},
        {"not json", "$: the session is not a JSON object"},
    };
    for (const auto& entry : broken) {
        CAPTURE(entry.text);
        const auto parsed = gs::script::parseSession(entry.text);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().starts_with(entry.problem));
    }
}

TEST_CASE("SCR-011: each profile gets typed controls and states, with ids that are not identifiers quoted")
{
    const std::string types = gs::script::profileTypes(
        {{"jump", "Jump", "tap", true}, {"dash-move", "Dash */ now", "hold", true}, {"2nd", "", "camera", true}},
        {{"aiming", "Aiming", false}, {"boat", "", false}});
    CHECK(types
          == "interface Controls {\n"
             "    /** Jump · tap */\n"
             "    readonly jump: Tap & { readonly position: PicturePoint };\n"
             "    /** Dash *\\/ now · hold */\n"
             "    readonly \"dash-move\": Hold & { readonly position: PicturePoint };\n"
             "    /** camera */\n"
             "    readonly \"2nd\": Camera & { readonly position: PicturePoint };\n"
             "}\n\n"
             "interface Vars {\n"
             "    /** Aiming */\n"
             "    readonly aiming: Variable;\n"
             "    readonly boat: Variable;\n"
             "}\n");
}

namespace {

constexpr double kEvenFrames = 50.0;

constexpr std::string_view kResources = R"({"format": "gamescrcpy.resources", "version": 1,
 "templates": [{"name": "开始按钮", "file": "开始按钮.png", "threshold": 0.9, "match": "color", "region": "右下角",
                "source": {"width": 1080, "height": 2400}}],
 "regions": [{"name": "右下角", "x": 0.5, "y": 0.6, "width": 0.5, "height": 0.4}],
 "points": [{"name": "活动入口", "x": 0.12, "y": 0.08}],
 "paths": [{"name": "拖动地图", "points": [[0.1, 0.8], [0.3, 0.75]], "durationMs": 400}],
 "colors": [{"name": "活动横幅", "color": "#F5C542", "tolerance": 0.1, "region": "右下角"}]})";

constexpr std::string_view kCodeOf = R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
)js";

std::vector<StateInfo> statesNamed(std::initializer_list<const char*> ids)
{
    std::vector<StateInfo> states;
    for (const char* id : ids) {
        states.push_back({id, id, false});
    }
    return states;
}

}

TEST_CASE("SCR-013: until checks its condition once a frame after the action and resolves with the first truthy value")
{
    const Replayed seen = replayScript(R"js(
let checks = 0;
controls.go.onPress(async () => {
    const seen = await touch.tap(0.5, 0.5).until(() => {
        checks += 1;
        return vars.ready.value && "ready";
    });
    log.info("seen", seen, checks);
});
)js",
                                       {press(0, "go"), stateChange(130, "ready", true)}, controlsNamed({"go"}),
                                       statesNamed({"ready"}), 1, kEvenFrames);
    CHECK(seen.lines == std::vector<std::string>{"0 tap 0.5 0.5 60", "140 # log seen ready 6"});
}

TEST_CASE("SCR-013: an async condition is awaited, such as vision.find, and a hold or key press waits the same way")
{
    const Replayed later = replayScript(R"js(
controls.go.onPress(async () => {
    const seen = await device.pressKey("back").until(async () => {
        await sleep(5);
        return vars.ready.value ? { at: time.now() } : null;
    });
    log.info("seen at", seen.at);
});
)js",
                                        {press(0, "go"), stateChange(100, "ready", true)}, controlsNamed({"go"}),
                                        statesNamed({"ready"}), 1, kEvenFrames);
    CHECK(later.lines == std::vector<std::string>{"0 key back down", "60 key back up", "110 # log seen at 110"});

    const Replayed looked = replayScript(R"js(
controls.fire.onPress(async () => {
    try {
        await controls.fire.hold(30).until(() => vision.find("res/next.png"), { timeoutMs: 20 });
    } catch (error) {
        log.info(error.name, error.message);
    }
});
)js",
                                         {press(0, "fire")}, controlsNamed({"fire"}), {}, 1, kEvenFrames);
    CHECK(looked.lines == std::vector<std::string>{
                              "0 control fire down", "30 control fire up", "30 vision find res/next.png",
                              "30 vision find res/next.png", "50 vision find res/next.png",
                              "50 # log TimeoutError GS-API-030 the condition did not come true within 20 ms of the "
                              "action (tried once)"});
}

TEST_CASE("SCR-013: retry does the action again with a new deadline, then until fails with GS-API-030 where it was called")
{
    const Replayed retried = replayScript(R"js(
controls.go.onPress(async () => {
    try {
        await touch.tap(0.5, 0.5).until(() => false, { timeoutMs: 100, retry: 2 });
    } catch (error) {
        log.info(error.name, error.message);
    }
});
)js",
                                          {press(0, "go")}, controlsNamed({"go"}), {}, 1, kEvenFrames);
    CHECK(retried.lines == std::vector<std::string>{"0 tap 0.5 0.5 60", "160 tap 0.5 0.5 60", "320 tap 0.5 0.5 60",
                                                    "480 # log TimeoutError GS-API-030 the condition did not come "
                                                    "true within 100 ms of the action (tried 3 times)"});

    const Replayed failed = replayScript(R"js(
export async function fail() {
    await device.pressKey("back").until(() => false, { timeoutMs: 1 });
}
)js",
                                         {run(0, "fail")}, {}, {}, 1, kEvenFrames);
    CHECK(failed.lines == std::vector<std::string>{"0 key back down", "60 key back up",
                                                   "80 # error TimeoutError: GS-API-030 the condition did not come "
                                                   "true within 1 ms of the action (tried once)"});

    ScriptFolder folder;
    folder.write("main.js", "export async function fail() {\n"
                            "    await device.pressKey(\"back\").until(() => false, { timeoutMs: 1 });\n"
                            "}\n");
    ManualClock clock;
    RecordingServices services(clock, DeviceInfo{"Pixel", 15, 1080, 2400, 0}, {}, {});
    std::vector<Report> failures;
    const ScriptHost::Sink sink = [&failures](const Report& report) {
        if (report.kind == ReportKind::TaskFailed) {
            failures.push_back(report);
        }
    };
    ReplayOptions options;
    options.source = folder.source();
    options.events = {run(0, "fail")};
    static_cast<void>(replay(options, services, clock, sink));
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].location.module == "main.js");
    CHECK(failures[0].location.line == 2);
}

TEST_CASE("SCR-013: cancelling the task ends the wait, or the action itself, with an AbortError and no more tries")
{
    const Replayed cancelled = replayScript(R"js(
controls.go.whileHeld(async () => {
    try {
        await touch.tap(0.5, 0.5).until(() => false, { timeoutMs: 10000, retry: 3 });
    } catch (error) {
        log.info("go", error.name);
    }
});
controls.dash.whileHeld(async () => {
    try {
        await touch.tap(0.2, 0.2).until(() => false);
    } catch (error) {
        log.info("dash", error.name);
    }
});
)js",
                                            {press(0, "go"), release(100, "go"), press(200, "dash"),
                                             release(230, "dash")},
                                            controlsNamed({"go", "dash"}), {}, 1, kEvenFrames);
    CHECK(cancelled.lines == std::vector<std::string>{"0 tap 0.5 0.5 60", "100 # log go AbortError",
                                                      "200 tap 0.2 0.2 60", "230 lift tap 0.2 0.2 60",
                                                      "230 # log dash AbortError"});
}

TEST_CASE("SCR-005: a cancelled task lifts the finger of its tap or swipe at once, a finished one is not lifted again")
{
    const Replayed lifted = replayScript(R"js(
controls.hold.whileHeld(async ({ signal }) => {
    while (!signal.aborted) await touch.tap(0.5, 0.5, 60000);
});
controls.drag.whileHeld(async () => {
    await touch.swipe(0.1, 0.2, 0.9, 0.2, 400);
});
controls.quick.whileHeld(async () => {
    await touch.tap(0.3, 0.3, 50);
    await sleep(1000);
});
export function forever() {
    void touch.tap(0.7, 0.7, 60000);
}
)js",
                                         {press(0, "hold"), release(120, "hold"), press(200, "drag"),
                                          release(300, "drag"), press(400, "quick"), release(600, "quick"),
                                          run(700, "forever")},
                                         controlsNamed({"hold", "drag", "quick"}));
    CHECK(lifted.lines == std::vector<std::string>{"0 tap 0.5 0.5 60000", "120 lift tap 0.5 0.5 60000",
                                                   "200 swipe 0.1 0.2 0.9 0.2 400",
                                                   "300 lift swipe 0.1 0.2 0.9 0.2 400", "400 tap 0.3 0.3 50",
                                                   "700 tap 0.7 0.7 60000", "10000 lift tap 0.7 0.7 60000"});
    CHECK(lifted.result.reachedLimit);
}

TEST_CASE("SCR-013: every action has until, plain await is unchanged, and wrong arguments get stable error codes")
{
    const Replayed wrong = replayScript(std::string(kCodeOf) + R"js(
controls.go.onPress(() => {
    const tap = touch.tap(0.5, 0.5);
    log.info(codeOf(() => tap.until(42)), codeOf(() => tap.until(() => true, 5)),
             codeOf(() => tap.until(() => true, { timeoutMs: 0 })), codeOf(() => tap.until(() => true, { timeoutMs: 600001 })),
             codeOf(() => tap.until(() => true, { retry: 11 })), codeOf(() => tap.until(() => true, { retry: 1.5 })),
             codeOf(() => tap.until(() => true, { retry: -1 })),
             codeOf(() => tap.until(() => true, { timeoutMs: 600000, retry: 10 })));
    const actions = [touch.swipe(0, 0, 1, 1), device.pressKey("back"), controls.go.trigger(), controls.go.hold(10),
                     mapping.trigger("go"), mapping.hold("go", 10)];
    log.info(tap instanceof Promise, Object.keys(tap).length,
             actions.every((action) => action instanceof Promise && typeof action.until === "function"),
             touch.tap.name, touch.tap.length, mapping.hold.length, Object.isFrozen(touch));
});
)js",
                                        {press(0, "go")}, controlsNamed({"go"}));
    CHECK(wrong.lines == std::vector<std::string>{
                             "0 tap 0.5 0.5 60",
                             "0 # log GS-API-008 GS-API-011 GS-API-003 GS-API-003 GS-API-017 GS-API-017 GS-API-017 none",
                             "0 swipe 0 0 1 1 300", "0 key back down", "0 control go down", "0 control go down",
                             "0 control go down", "0 control go down", "0 # log true 0 true tap 1 2 true",
                             "10 control go up", "10 control go up", "60 key back up", "60 control go up",
                             "60 control go up"});
}

TEST_CASE("SCR-013, SCR-012: the declarations type the actions as Action<void> with until and give controls bind")
{
    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const auto found = std::ranges::find(files, std::string("types/gamescrcpy.d.ts"), &gs::script::ApiFile::path);
    REQUIRE(found != files.end());
    for (const std::string_view line :
         {"type Action<T> = Promise<T> & { until<R>(condition: () => R | Promise<R>, options?: UntilOptions): "
          "Promise<NonNullable<Awaited<R>>> };",
          "type UntilOptions = { readonly timeoutMs?: number; readonly retry?: number };",
          "type Binding = { dispose(): void };",
          "    function tap(x: number, y: number, durationMs?: number): Action<void>;",
          "    function swipe(fromX: number, fromY: number, toX: number, toY: number, durationMs?: number): "
          "Action<void>;",
          "    function pressKey(key: AndroidKey): Action<void>;", "    function trigger(id: string): Action<void>;",
          "    function hold(id: string, durationMs: number): Action<void>;", "    trigger(): Action<void>;",
          "    hold(durationMs: number): Action<void>;",
          "    bind(property: \"enabled\", compute: () => unknown): Binding;",
          "     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try."}) {
        CAPTURE(line);
        CHECK(found->contents.find(line) != std::string::npos);
    }
}

TEST_CASE("SCR-012: a binding follows the states it read last time, changed by the session or by the script")
{
    const Replayed followed = replayScript(R"js(
let runs = 0;
controls.fire.bind("enabled", () => {
    runs += 1;
    return vars.boat.value ? !vars.aiming.value : true;
});
export function aim() {
    vars.aiming.value = !vars.aiming.value;
}
export function report() {
    log.info("runs", runs, controls.fire.enabled);
}
)js",
                                           {stateChange(100, "aiming", true), stateChange(200, "boat", true),
                                            run(300, "aim"), stateChange(350, "boat", true), run(400, "report")},
                                           controlsNamed({"fire"}), statesNamed({"aiming", "boat"}));
    CHECK(followed.lines == std::vector<std::string>{"200 enable fire off", "300 state aiming off",
                                                     "300 enable fire on", "400 # log runs 3 true"});
}

TEST_CASE("SCR-012: disposing a binding stops it and puts back the value from before it")
{
    const Replayed disposed = replayScript(R"js(
const binding = controls.fire.bind("enabled", () => !vars.aiming.value);
export function stop() {
    binding.dispose();
    binding.dispose();
    log.info(controls.fire.enabled);
}
)js",
                                           {stateChange(100, "aiming", true), run(200, "stop"),
                                            stateChange(300, "aiming", false), stateChange(400, "aiming", true)},
                                           controlsNamed({"fire"}), statesNamed({"aiming"}));
    CHECK(disposed.lines
          == std::vector<std::string>{"100 enable fire off", "200 enable fire on", "200 # log true"});
}

TEST_CASE("SCR-012: a binding made in a task ends with it, so one made in whileHeld puts the value back on release")
{
    const Replayed owned = replayScript(R"js(
controls.aim.whileHeld(async () => {
    controls.fire.bind("enabled", () => false);
    await new Promise(() => {});
});
controls.jump.onPress(() => {
    controls.fire.bind("enabled", () => vars.aiming.value);
});
)js",
                                        {press(100, "aim"), release(200, "aim"), press(300, "jump")},
                                        controlsNamed({"aim", "fire", "jump"}), statesNamed({"aiming"}));
    CHECK(owned.lines == std::vector<std::string>{"100 enable fire off", "200 enable fire on", "300 enable fire off",
                                                  "300 enable fire on"});
}

TEST_CASE("SCR-012: binding a property again takes over without putting the value back first")
{
    const Replayed taken = replayScript(R"js(
const first = controls.fire.bind("enabled", () => vars.boat.value);
const second = controls.fire.bind("enabled", () => vars.aiming.value);
export function drop() {
    first.dispose();
    second.dispose();
}
)js",
                                        {stateChange(100, "boat", true), stateChange(200, "aiming", true),
                                         stateChange(250, "aiming", false), run(300, "drop"),
                                         stateChange(400, "boat", false), stateChange(450, "aiming", true)},
                                        controlsNamed({"fire"}), statesNamed({"aiming", "boat"}));
    CHECK(taken.lines == std::vector<std::string>{"0 enable fire off", "200 enable fire on", "250 enable fire off",
                                                  "300 enable fire on"});
}

TEST_CASE("SCR-012: setting a bound property directly ends the binding and keeps the value set")
{
    const Replayed assigned = replayScript(R"js(
controls.fire.bind("enabled", () => !vars.aiming.value);
export function force() {
    controls.fire.enabled = false;
}
export function report() {
    log.info(controls.fire.enabled);
}
)js",
                                           {stateChange(100, "aiming", true), stateChange(200, "aiming", false),
                                            run(300, "force"), stateChange(400, "aiming", true),
                                            stateChange(500, "aiming", false), run(600, "report")},
                                           controlsNamed({"fire"}), statesNamed({"aiming"}));
    CHECK(assigned.lines == std::vector<std::string>{"100 enable fire off", "200 enable fire on", "300 enable fire off",
                                                     "600 # log false"});

    const Replayed mapped = replayScript(R"js(
controls.fire.bind("enabled", () => !vars.aiming.value);
export function force() {
    mapping.setEnabled("fire", false);
}
export function report() {
    log.info(controls.fire.enabled);
}
)js",
                                         {stateChange(100, "aiming", true), stateChange(200, "aiming", false),
                                          run(300, "force"), stateChange(400, "aiming", true),
                                          stateChange(500, "aiming", false), run(600, "report")},
                                         controlsNamed({"fire"}), statesNamed({"aiming"}));
    CHECK(mapped.lines == std::vector<std::string>{"100 enable fire off", "200 enable fire on", "300 enable fire off",
                                                   "600 # log false"});
}

TEST_CASE("SCR-012: a compute that throws is reported and keeps the last value until it works again")
{
    const Replayed thrown = replayScript(R"js(
controls.fire.bind("enabled", () => {
    if (vars.boat.value) {
        throw new Error("no boats here");
    }
    return !vars.aiming.value;
});
)js",
                                         {stateChange(100, "aiming", true), stateChange(200, "boat", true),
                                          stateChange(300, "aiming", false), stateChange(400, "boat", false)},
                                         controlsNamed({"fire"}), statesNamed({"aiming", "boat"}));
    CHECK(thrown.lines == std::vector<std::string>{"100 enable fire off", "200 # error Error: no boats here",
                                                   "300 # error Error: no boats here", "400 enable fire on"});
}

TEST_CASE("SCR-012: a compute that returns a Promise gives GS-API-032, at once or later, and ends the binding")
{
    const Replayed promised = replayScript(std::string(kCodeOf) + R"js(
log.info(codeOf(() => controls.fire.bind("enabled", async () => true)), controls.fire.enabled);
controls.fire.bind("enabled", () => (vars.boat.value ? Promise.resolve(false) : !vars.aiming.value));
)js",
                                           {stateChange(100, "aiming", true), stateChange(200, "boat", true),
                                            stateChange(300, "boat", false), stateChange(400, "aiming", false)},
                                           controlsNamed({"fire"}), statesNamed({"aiming", "boat"}));
    CHECK(promised.lines == std::vector<std::string>{
                                "0 # log GS-API-032 true", "100 enable fire off", "200 enable fire on",
                                "200 # error TypeError: GS-API-032 a binding's compute must return its value at once, "
                                "not a Promise"});
}

TEST_CASE("SCR-012: only enabled can be bound, with a function, and wrong bindings change nothing")
{
    const Replayed refused = replayScript(std::string(kCodeOf) + R"js(
log.info(codeOf(() => controls.fire.bind("visible", () => true)), codeOf(() => controls.fire.bind("enabled", 42)),
         codeOf(() => controls.fire.bind()), codeOf(() => controls.fire.bind("enabled")), controls.fire.enabled);
)js",
                                          {}, controlsNamed({"fire"}));
    CHECK(refused.lines == std::vector<std::string>{"0 # log GS-API-031 GS-API-008 GS-API-031 GS-API-008 true"});
}

TEST_CASE("resources: scripts use the names of res/resources.json, and unknown names are refused with GS-API-018 (VIS-004)")
{
    ScriptFolder folder;
    folder.write("res/resources.json", kResources);
    folder.write("main.js", std::string(kCodeOf) + R"js(
controls.jump.onPress(async () => {
    const start = await vision.find("开始按钮");
    const all = await vision.findAll("开始按钮", { region: "右下角", threshold: 0.5, match: "grey" });
    const banner = await vision.findColor("活动横幅");
    const gold = await vision.findColor("#F5C542", { tolerance: 0.2, region: { x: 0, y: 0, width: 0.5, height: 0.5 } });
    log.info(start, all.length, banner, gold);
});
log.info(JSON.stringify(res.point("活动入口")), JSON.stringify(res.region("右下角")), JSON.stringify(res.path("拖动地图")));
log.info(codeOf(() => vision.find("开始")), codeOf(() => vision.find("res/x.png", { region: "左上角" })),
         codeOf(() => vision.findColor("暗红")), codeOf(() => vision.findColor("#F5C54")), codeOf(() => vision.findColor(3)),
         codeOf(() => vision.findColor("#F5C542", { tolerance: 2 })), codeOf(() => vision.find("开始按钮", { match: "sepia" })),
         codeOf(() => res.point("右下角")), codeOf(() => res.path(7)), codeOf(() => vision.find("bad/name")));
try {
    res.region("没有");
} catch (error) {
    log.info(error.message);
}
)js");
    const Replayed found = replayIn(folder, {press(10, "jump")}, controlsNamed({"jump"}));
    CHECK(found.lines
          == std::vector<std::string>{
              "0 # log {\"x\":0.12,\"y\":0.08} {\"x\":0.5,\"y\":0.6,\"width\":0.5,\"height\":0.4} "
              "{\"points\":[{\"x\":0.1,\"y\":0.8},{\"x\":0.3,\"y\":0.75}],\"durationMs\":400}",
              "0 # log GS-API-018 GS-API-018 GS-API-018 GS-API-019 GS-API-019 GS-API-017 GS-API-019 GS-API-018 "
              "GS-API-018 GS-API-015",
              "0 # log GS-API-018 there is no region named \"没有\"",
              "10 vision find 开始按钮",
              "10 vision findAll 开始按钮",
              "10 vision findColor 活动横幅",
              "10 vision findColor #F5C542",
              "10 # log null 0 null null"});
}

TEST_CASE("resources: a broken res/resources.json refuses names with GS-API-020 while paths keep working (VIS-004)")
{
    ScriptFolder folder;
    folder.write("res/resources.json", R"({"format": "gamescrcpy.resources", "version": 1, "points": [{"name": "p"}]})");
    folder.write("main.js", std::string(kCodeOf) + R"js(
log.info(codeOf(() => res.point("p")), codeOf(() => vision.find("chest")), codeOf(() => vision.findColor("gold")),
         codeOf(() => vision.find("res/chest.png", { region: "r" })), codeOf(() => vision.findColor("#000000")));
try {
    res.point("p");
} catch (error) {
    log.info(error.message);
}
controls.jump.onPress(async () => {
    log.info(await vision.find("res/chest.png"));
});
)js");
    const Replayed found = replayIn(folder, {press(10, "jump")}, controlsNamed({"jump"}));
    CHECK(found.lines
          == std::vector<std::string>{"0 vision findColor #000000",
                                      "0 # log GS-API-020 GS-API-020 GS-API-020 GS-API-020 none",
                                      "0 # log GS-API-020 res/resources.json cannot be used: $.points[0].x: is missing",
                                      "10 vision find res/chest.png",
                                      "10 # log null"});
}

TEST_CASE("SCR-011: the resource names of the profile are typed, so a misspelt name fails the type check (VIS-004)")
{
    const auto resources = gs::script::parseResources(kResources);
    REQUIRE(resources);
    CHECK(gs::script::profileTypes({}, {}, *resources)
          == "interface Controls {\n"
             "}\n\n"
             "interface Vars {\n"
             "}\n"
             "\ninterface TemplateNames {\n"
             "    /** res/开始按钮.png · threshold 0.9 · color */\n"
             "    readonly \"开始按钮\": true;\n"
             "}\n"
             "\ninterface RegionNames {\n"
             "    /** x 0.5 y 0.6 · 0.5 × 0.4 */\n"
             "    readonly \"右下角\": true;\n"
             "}\n"
             "\ninterface PointNames {\n"
             "    /** x 0.12 y 0.08 */\n"
             "    readonly \"活动入口\": true;\n"
             "}\n"
             "\ninterface PathNames {\n"
             "    /** 2 points · 400 ms */\n"
             "    readonly \"拖动地图\": true;\n"
             "}\n"
             "\ninterface ColorNames {\n"
             "    /** #F5C542 · tolerance 0.1 */\n"
             "    readonly \"活动横幅\": true;\n"
             "}\n");
    CHECK(gs::script::profileTypes({}, {}, {}) == "interface Controls {\n}\n\ninterface Vars {\n}\n");
}

TEST_CASE("SCR-015: a state machine run against a recording leaves each state and its reason in the transcript")
{
    ScriptFolder folder;
    folder.write("main.js", R"js(
export async function play() {
    await machine({
        async lobby(to) {
            await sleep(100);
            return to.queue("tapped start");
        },
        async queue(to) {
            await sleep(200);
            return to.game();
        },
        async game() {
            await sleep(50);
        },
    }, { name: "afk" });
}
)js");
    ManualClock clock;
    RecordingServices services(clock, DeviceInfo{"Pixel", 15, 1080, 2400, 0}, {}, {});
    const ScriptHost::Sink sink = [](const Report&) {};
    ReplayOptions options;
    options.source = folder.source();
    options.events = {run(0, "play")};
    options.until = Microseconds{1'000'000};
    static_cast<void>(replay(options, services, clock, sink));
    std::vector<std::string> lines;
    for (const auto& line : services.lines()) {
        lines.push_back(formatLine(line));
    }
    CHECK(lines == std::vector<std::string>{"0 # machine afk: lobby", "100 # machine afk: queue (tapped start)",
                                            "300 # machine afk: game", "350 # machine afk: ended"});
    CHECK(services.outputs().empty());
    const std::vector<ReplayMachineState>& states = services.machineStates();
    REQUIRE(states.size() == 4);
    CHECK(states[0].at == Microseconds{0});
    CHECK(states[0].state == "lobby");
    CHECK(states[0].reason.empty());
    CHECK(states[1].at == Microseconds{100'000});
    CHECK(states[1].state == "queue");
    CHECK(states[1].reason == "tapped start");
    CHECK(states[1].name == "afk");
    CHECK(states[1].file == "main.js");
    CHECK(states[1].line > 0);
    CHECK(states[2].state == "game");
    CHECK(states[3].at == Microseconds{350'000});
    CHECK(states[3].state.empty());
    CHECK(services.app("other", "{}") == "null");
    CHECK(services.app("machineState", "not json") == "null");
    CHECK(services.machineStates().size() == 4);
}
