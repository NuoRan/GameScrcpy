#include "ScriptFixture.h"

#include "gs/script/ApiFiles.h"
#include "gs/script/Services.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::Point;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::StateInfo;
using gs::script::TaskId;
using gs::script::test::kindIs;
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

class FakeServices final : public gs::script::ScriptServices
{
public:
    DeviceInfo device() override { return {"Pixel", 15, 1080, 2400, 0}; }

    void tap(std::uint64_t, Point at, double durationMs) override
    {
        record(std::format("tap {} {} {}", at.x, at.y, durationMs));
    }

    void swipe(std::uint64_t, Point from, Point to, double durationMs) override
    {
        record(std::format("swipe {} {} {} {} {}", from.x, from.y, to.x, to.y, durationMs));
    }

    void lift(std::uint64_t) override { record("lift"); }

    void key(std::string_view name, bool down) override
    {
        record(std::format("key {} {}", name, down ? "down" : "up"));
    }

    std::vector<ControlInfo> controls() override
    {
        const std::scoped_lock lock(m_mutex);
        return m_controls;
    }

    bool pressControl(std::string_view id, bool down) override
    {
        if (!known(id)) {
            return false;
        }
        record(std::format("control {} {}", id, down ? "down" : "up"));
        return true;
    }

    bool enableControl(std::string_view id, bool enabled) override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = std::ranges::find(m_controls, id, &ControlInfo::id);
        if (found == m_controls.end()) {
            return false;
        }
        found->enabled = enabled;
        m_events.push_back(std::format("enable {} {}", id, enabled ? "on" : "off"));
        return true;
    }

    std::vector<StateInfo> states() override
    {
        const std::scoped_lock lock(m_mutex);
        return m_states;
    }

    std::optional<gs::script::VarValue> stateValue(std::string_view id) override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = std::ranges::find(m_states, id, &StateInfo::id);
        if (found == m_states.end()) {
            return std::nullopt;
        }
        return found->value;
    }

    bool setState(std::string_view id, const gs::script::VarValue& value) override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = std::ranges::find(m_states, id, &StateInfo::id);
        if (found == m_states.end()) {
            return false;
        }
        found->value = value;
        const bool* on = std::get_if<bool>(&value);
        m_events.push_back(
            std::format("state {} {}", id, on != nullptr ? (*on ? "on" : "off") : gs::script::valueJson(value)));
        return true;
    }

    bool controlAction(std::string_view id, std::string_view action, Point at) override
    {
        if (!id.empty() && !known(id)) {
            return false;
        }
        record(std::format("act {} {} {} {}", id, action, at.x, at.y));
        return true;
    }

    std::optional<gs::script::ControlStatus> controlStatus(std::string_view id) override
    {
        if (!known(id)) {
            return std::nullopt;
        }
        const std::scoped_lock lock(m_mutex);
        return m_status;
    }

    void setStatus(const gs::script::ControlStatus& status)
    {
        const std::scoped_lock lock(m_mutex);
        m_status = status;
    }

    void setProps(std::string_view id, std::string props)
    {
        const std::scoped_lock lock(m_mutex);
        for (ControlInfo& control : m_controls) {
            if (control.id == id) {
                control.props = props;
            }
        }
    }

    void addControl(ControlInfo info)
    {
        const std::scoped_lock lock(m_mutex);
        m_controls.push_back(std::move(info));
    }

    bool claimKey(std::string_view name) override
    {
        if (name == "Nope") {
            return false;
        }
        record(std::format("claim {}", name));
        return true;
    }

    void handOver(std::string_view id, bool on) override
    {
        const std::scoped_lock lock(m_mutex);
        m_handed.push_back(std::format("{} {}", id, on ? "on" : "off"));
    }

    std::vector<std::string> handed() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_handed;
    }

    void type(std::string_view text) override { record(std::format("type {}", text)); }

    void hold(std::uint64_t, Point at) override { record(std::format("finger down {} {}", at.x, at.y)); }

    void move(std::uint64_t, Point to, double durationMs) override
    {
        record(std::format("finger move {} {} {}", to.x, to.y, durationMs));
    }

    void liftAll() override { record("lift all"); }

    std::optional<std::string> globalValue(std::string_view name) override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_globals.find(std::string(name));
        return found != m_globals.end() ? std::optional<std::string>(found->second) : std::nullopt;
    }

    void setGlobal(std::string_view name, std::string_view json) override
    {
        const std::scoped_lock lock(m_mutex);
        m_globals[std::string(name)] = std::string(json);
        m_events.push_back(std::format("global {} {}", name, json));
    }

    std::string app(std::string_view request, std::string_view json) override
    {
        record(std::format("app {} {}", request, json));
        if (request == "profile" && json == "null") {
            return R"({"id":"p1","name":"Shooter"})";
        }
        if (request == "taskParams") {
            return R"({"rounds":2,"map":7})";
        }
        if (request == "focused") {
            const std::scoped_lock lock(m_mutex);
            return m_focused ? "true" : "false";
        }
        return "true";
    }

    void readClipboard(std::uint64_t request) override { requested(request, "clipboard read"); }

    void writeClipboard(std::uint64_t request, std::string_view text) override
    {
        requested(request, std::format("clipboard write {}", text));
    }

    std::optional<std::uint64_t> waitForRequest(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_requested.wait_for(lock, std::chrono::seconds(30), [&] { return m_requests.size() > index; })) {
            return std::nullopt;
        }
        return m_requests[index];
    }

    void setFocused(bool focused)
    {
        const std::scoped_lock lock(m_mutex);
        m_focused = focused;
    }

    bool connected() override
    {
        const std::scoped_lock lock(m_mutex);
        return m_connected;
    }

    void awaitConnection(std::uint64_t request) override { requested(request, "await connection"); }

    void setConnected(bool connected)
    {
        const std::scoped_lock lock(m_mutex);
        m_connected = connected;
    }

    void draw(std::uint64_t, const gs::script::Drawing& drawing) override
    {
        static constexpr std::string_view kKinds[] = {"box", "text", "mark", "image"};
        if (drawing.kind == gs::script::DrawingKind::Image) {
            record(std::format("draw image {} {} {} {} {} {}",
                               drawing.capture != 0 ? std::format("capture {}", drawing.capture) : drawing.image,
                               drawing.at.x, drawing.at.y, drawing.width, drawing.height, drawing.color));
            return;
        }
        record(std::format("draw {} {} {} {}{}{}", kKinds[static_cast<std::size_t>(drawing.kind)], drawing.at.x,
                           drawing.at.y, drawing.color, drawing.text.empty() ? "" : " ", drawing.text));
    }

    void find(std::uint64_t request, const gs::script::FindQuery& query) override
    {
        requested(request, std::format("find {} {} {} {}", query.image, query.capture, query.threshold,
                                       query.match == gs::script::MatchMode::Color ? "color" : "grey"));
    }

    void capture(std::uint64_t request, const gs::script::CaptureQuery& query) override
    {
        requested(request, std::format("capture {} {} {} {} {}", query.region.x, query.region.y, query.region.width,
                                       query.region.height, query.timeoutMs));
    }

    void changed(std::uint64_t request, const gs::script::ChangeQuery& query) override
    {
        requested(request, std::format("changed {} {} {}", query.key, query.region.width, query.threshold));
    }

    void readText(std::uint64_t request, const gs::script::TextQuery& query) override
    {
        requested(request, std::format("{} [{}] {} {} {} {} [{}] {}", query.find ? "findText" : "readText", query.text,
                                       query.region.x, query.region.y, query.region.width, query.region.height,
                                       query.language, query.timeoutMs));
    }

    void saveCapture(std::uint64_t request, std::uint64_t capture, std::string_view name) override
    {
        requested(request, std::format("save {} {}", capture, name));
    }

    std::optional<std::string> tuneControl(std::string_view id, std::string_view change) override
    {
        if (!known(id)) {
            return std::nullopt;
        }
        record(std::format("tune {} {}", id, change));
        return std::string(R"({"inputs":{"press":"Mouse.Left"},"rateHz":12,"at":[0.25,0.75]})");
    }

    void saveProfile(std::uint64_t request) override { requested(request, "save profile"); }

    void erase(std::uint64_t) override { record("erase"); }

    void pickColor(std::uint64_t request, const gs::script::PixelQuery& query) override
    {
        if (query.match) {
            record(std::format("matchColor {} {} {} {:02X}{:02X}{:02X} {} {}", query.at.x, query.at.y, query.color,
                               query.rgb.red, query.rgb.green, query.rgb.blue, query.tolerance, query.timeoutMs));
        } else {
            record(std::format("pickColor {} {} {}", query.at.x, query.at.y, query.timeoutMs));
        }
        const std::scoped_lock lock(m_mutex);
        m_picks.push_back(request);
    }

    void cancelFind(std::uint64_t request) override
    {
        const std::scoped_lock lock(m_mutex);
        m_events.push_back(std::ranges::find(m_picks, request) != m_picks.end() ? "cancel pick" : "cancel");
    }

    std::vector<std::uint64_t> picks() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_picks;
    }

    std::optional<std::string> loadStore(std::string_view owner) override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_stores.find(std::string(owner));
        if (found == m_stores.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    void saveStore(std::string_view owner, std::string_view contents) override
    {
        const std::scoped_lock lock(m_mutex);
        m_stores[std::string(owner)] = std::string(contents);
    }

    std::vector<std::string> events() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_events;
    }

    std::map<std::string, std::string> stores() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_stores;
    }

private:
    bool known(std::string_view id) const
    {
        const std::scoped_lock lock(m_mutex);
        return std::ranges::find(m_controls, id, &ControlInfo::id) != m_controls.end();
    }

    void record(std::string event)
    {
        const std::scoped_lock lock(m_mutex);
        m_events.push_back(std::move(event));
    }

    void requested(std::uint64_t request, std::string event)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_events.push_back(std::move(event));
            m_requests.push_back(request);
        }
        m_requested.notify_all();
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_requested;
    std::vector<std::uint64_t> m_requests;
    std::vector<std::string> m_events;
    std::vector<ControlInfo> m_controls{{"jump", "Jump", "tap", true, R"({"inputs":{"press":"Space"},"tapMs":40})"},
                                        {"fire", "Fire", "turbo", true, R"({"inputs":{"press":"Mouse.Left"},"rateHz":12})"},
                                        {"move", "Move", "wasd", true, R"({"inputs":{"up":"KeyW","down":"KeyS"}})"}};
    std::vector<StateInfo> m_states{{"aiming", "Aiming", false}, {"round", "Round", 1.0}};
    std::map<std::string, std::string> m_stores;
    std::map<std::string, std::string> m_globals;
    gs::script::ControlStatus m_status;
    std::vector<std::uint64_t> m_picks;
    std::vector<std::string> m_handed;
    bool m_focused = false;
    bool m_connected = true;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<FakeServices> services = std::make_shared<FakeServices>();
    std::unique_ptr<ScriptHost> host;

    explicit Fixture(bool withServices = true)
        : host(std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{},
                                            withServices ? services : nullptr))
    {}

    ScriptId load(std::string_view main)
    {
        folder.write("main.js", main);
        ScriptId id = 0;
        const auto report = loadScript(*host, recorder, folder.source(), &id);
        REQUIRE(report.has_value());
        INFO(gs::script::describe(*report));
        REQUIRE(report->kind == ReportKind::ScriptLoaded);
        return id;
    }

    Report run(ScriptId script, const std::string& handler)
    {
        const TaskId task = host->start(script, handler);
        const auto ended = recorder.waitFor(taskEnded(task));
        REQUIRE(ended.has_value());
        return ended->report;
    }
};

}

TEST_CASE("SCR-005: touch, keys and mapping reach the phone in picture ratios and resolve after the gesture")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function play() {
    const started = time.now();
    await touch.tap(0.5, 0.8);
    await touch.swipe(0.1, 0.2, 0.3, 0.4, 120);
    await device.back();
    await mapping.trigger("jump");
    await mapping.hold("fire", 30);
    mapping.setEnabled("jump", false);
    log.info(JSON.stringify(device.info()), mapping.controls().filter((c) => c.enabled).length);
    log.info("waited", time.now() - started >= 60 + 120 + 60 + 60 + 30);
}
)js");
    const Report ended = fixture.run(script, "play");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.services->events()
          == std::vector<std::string>{"tap 0.5 0.8 60", "swipe 0.1 0.2 0.3 0.4 120", "key back down", "key back up",
                                      "control jump down", "control jump up", "control fire down", "control fire up",
                                      "enable jump off"});
    CHECK(fixture.recorder.count(
              logged("{\"model\":\"Pixel\",\"android\":15,\"width\":1080,\"height\":2400,\"rotation\":0} 2"))
          == 1);
    CHECK(fixture.recorder.count(logged("waited true")) == 1);
}

TEST_CASE("SCR-005: actions made while the phone is reconnecting wait until it is back, and a cancelled one never acts")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function play() {
    log.info("connected", device.connected());
    await touch.tap(0.5, 0.5);
    log.info("tapped");
}
)js");
    fixture.services->setConnected(false);
    const TaskId task = fixture.host->start(script, "play");
    const std::optional<std::uint64_t> request = fixture.services->waitForRequest(0);
    REQUIRE(request.has_value());
    CHECK(fixture.services->events() == std::vector<std::string>{"await connection"});
    fixture.services->setConnected(true);
    fixture.host->answer(*request, "null");
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("connected false")) == 1);
    CHECK(fixture.recorder.count(logged("tapped")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"await connection", "tap 0.5 0.5 60"});

    fixture.services->setConnected(false);
    const TaskId cancelled = fixture.host->start(script, "play");
    const std::optional<std::uint64_t> second = fixture.services->waitForRequest(1);
    REQUIRE(second.has_value());
    fixture.host->cancel(cancelled);
    const auto stopped = fixture.recorder.waitFor(taskEnded(cancelled));
    REQUIRE(stopped.has_value());
    CHECK(stopped->report.kind == ReportKind::TaskCancelled);
    fixture.services->setConnected(true);
    fixture.host->answer(*second, "null");
    CHECK(fixture.run(script, "play").kind == ReportKind::TaskFinished);
    CHECK(fixture.services->events() == std::vector<std::string>{"await connection", "tap 0.5 0.5 60",
                                                                  "await connection", "tap 0.5 0.5 60"});
}

TEST_CASE("SCR-005: with a point, the duration may come second or third, as the blueprint writes it")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function play() {
    await touch.tap({ x: 0.25, y: 0.75 }, 80);
    await touch.tap({ x: 0.25, y: 0.75 }, undefined, 90);
    await touch.longPress({ x: 0.5, y: 0.5 }, undefined, 900);
    await touch.swipe({ x: 0.1, y: 0.2 }, { x: 0.3, y: 0.4 }, 100);
    await touch.swipe({ x: 0.1, y: 0.2 }, undefined, { x: 0.3, y: 0.4 }, undefined, 110);
    await touch.swipe({ x: 0.1, y: 0.2 }, undefined, 0.3, 0.4, 130);
    await touch.swipe(0.1, 0.2, { x: 0.3, y: 0.4 }, undefined, 140);
}
)js");
    const Report ended = fixture.run(script, "play");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.services->events()
          == std::vector<std::string>{"tap 0.25 0.75 80", "tap 0.25 0.75 90", "tap 0.5 0.5 900",
                                      "swipe 0.1 0.2 0.3 0.4 100", "swipe 0.1 0.2 0.3 0.4 110",
                                      "swipe 0.1 0.2 0.3 0.4 130", "swipe 0.1 0.2 0.3 0.4 140"});
}

TEST_CASE("SCR-005: two points with numbers after them swipe in the third number and leave the rest unused")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function play() {
    await touch.swipe({ x: 0.1, y: 0.2 }, { x: 0.3, y: 0.4 }, 0.5, 0.3, 10);
    await touch.swipe({ x: 0.1, y: 0.2 }, undefined, { x: 0.3, y: 0.4 }, undefined, 450);
    await touch.tap({ x: 0.25, y: 0.75 }, 80, 100);
}
)js");
    const Report ended = fixture.run(script, "play");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.services->events()
          == std::vector<std::string>{"swipe 0.1 0.2 0.3 0.4 0.5", "swipe 0.1 0.2 0.3 0.4 450",
                                      "tap 0.25 0.75 80"});
}

TEST_CASE("SCR-005: wrong arguments are refused with stable error codes and nothing reaches the phone")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
export function wrong() {
    log.info(codeOf(() => touch.tap(1.5, 0)), codeOf(() => touch.tap(0.5, 0.5, 0)), codeOf(() => device.pressKey("nope")),
             codeOf(() => mapping.trigger("missing")), codeOf(() => mapping.hold("fire")),
             codeOf(() => store.set("f", () => 1)), codeOf(() => store.set(3, 1)));
}
)js");
    CHECK(fixture.run(script, "wrong").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(
              logged("GS-API-002 GS-API-003 GS-API-004 GS-API-005 GS-API-003 GS-API-006 GS-API-006"))
          == 1);
    CHECK(fixture.services->events().empty());
}

TEST_CASE("SCR-005: a cancelled hold still releases its control")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function sprint() {
    log.info("holding");
    await mapping.hold("fire", 60000);
}
)js");
    const TaskId task = fixture.host->start(script, "sprint");
    REQUIRE(fixture.recorder.waitFor(logged("holding", task)).has_value());
    fixture.host->cancel(task);
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskCancelled);
    CHECK(fixture.services->events() == std::vector<std::string>{"control fire down", "control fire up"});
}

TEST_CASE("SCR-005: the store keeps JSON values per script folder across loads, within its quota")
{
    Fixture fixture;
    const ScriptId first = fixture.load(R"js(
export function save() {
    store.set("best", 42);
    store.set("names", ["a", "b"]);
    store.set("gone", true);
    store.remove("gone");
    const copy = store.get("names");
    copy.push("c");
    log.info(JSON.stringify(store.get("names")), store.keys().join(","), store.get("missing") === undefined);
    try {
        store.set("huge", "x".repeat(1100000));
    } catch (error) {
        log.info(error.message.split(" ")[0], store.keys().length);
    }
}
export function read() {
    log.info(store.get("best"), JSON.stringify(store.get("names")));
}
)js");
    CHECK(fixture.run(first, "save").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("[\"a\",\"b\"] best,names true")) == 1);
    CHECK(fixture.recorder.count(logged("GS-API-007 2")) == 1);
    const auto saved = fixture.services->stores();
    REQUIRE(saved.size() == 1);
    CHECK(saved.begin()->second == "{\"best\":42,\"names\":[\"a\",\"b\"]}");

    fixture.host->unload(first);
    ScriptId second = 0;
    const auto reloaded = loadScript(*fixture.host, fixture.recorder, fixture.folder.source(), &second);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.run(second, "read").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("42 [\"a\",\"b\"]")) == 1);
}

TEST_CASE("SCR-005: without a phone the functions that need one say so")
{
    Fixture fixture(false);
    const ScriptId script = fixture.load(R"js(
export async function tap() {
    try {
        await touch.tap(0.5, 0.5);
    } catch (error) {
        log.info(error.message.split(" ")[0], error.name);
    }
    log.info(typeof sleep, typeof nextFrame, typeof time.now(), Object.isFrozen(touch));
}
)js");
    CHECK(fixture.run(script, "tap").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-001 InternalError")) == 1);
    CHECK(fixture.recorder.count(logged("function function number true")) == 1);
}

TEST_CASE("SCR-006: the declarations, reference pages, completions and report come from the one table")
{
    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const auto contents = [&](std::string_view path) {
        const auto found = std::ranges::find(files, path, &gs::script::ApiFile::path);
        REQUIRE(found != files.end());
        return found->contents;
    };
    const std::string declarations = contents("types/gamescrcpy.d.ts");
    CHECK(declarations.find("declare function sleep(ms: number): Promise<void>;") != std::string::npos);
    CHECK(declarations.find("declare namespace touch {") != std::string::npos);
    CHECK(declarations.find("    function tap(x: number, y: number, durationMs?: number): Action<void>;")
          != std::string::npos);
    CHECK(declarations.find("    function tap(x: PicturePoint, y?: undefined, durationMs?: number): Action<void>;")
          != std::string::npos);
    CHECK(declarations.find("    function swipe(from: PicturePoint, to: PicturePoint, durationMs?: number): "
                            "Action<void>;")
          != std::string::npos);
    CHECK(declarations.find("    function info(...values: unknown[]): void;") != std::string::npos);
    CHECK(declarations.find("type AndroidKey = \"back\"") != std::string::npos);
    CHECK(declarations.find("interface Hold extends Component {") != std::string::npos);
    CHECK(declarations.find("interface PadStick extends Stick {}") != std::string::npos);
    CHECK(declarations.find("declare const keys: Keys;") != std::string::npos);
    CHECK(declarations.find("declare function every(intervalMs: number,") != std::string::npos);
    CHECK(declarations.find("@throws GS-API-002") != std::string::npos);
    for (const std::string_view name :
         {"log", "time", "touch", "device", "mapping", "hints", "profile", "store", "math"}) {
        CAPTURE(name);
        CHECK(declarations.find("declare namespace " + std::string(name) + " {") != std::string::npos);
        CHECK(contents("docs/" + std::string(name) + ".md").starts_with("# " + std::string(name) + "\n"));
    }
    CHECK(contents("docs/README.md").find("(0, 0) is the top left corner") != std::string::npos);
    CHECK(contents("completions.json").find("\"insertText\": \"touch.tap(${1:x})\"") != std::string::npos);
    CHECK(contents("api-report.json").find("\"signature\": \"mapping.hold(id: string, durationMs: number): Action<void>\"")
          != std::string::npos);
}

TEST_CASE("SCR-006: the reference pages can be written in another language, keeping names, types, examples and their structure")
{
    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const std::vector<gs::script::ApiFile> english = gs::script::apiDocs({});
    for (const gs::script::ApiFile& page : english) {
        CAPTURE(page.path);
        const auto found = std::ranges::find(files, page.path, &gs::script::ApiFile::path);
        REQUIRE(found != files.end());
        CHECK(found->contents == page.contents);
    }

    std::vector<std::string> asked;
    const std::vector<gs::script::ApiFile> other = gs::script::apiDocs([&asked](std::string_view text) {
        asked.emplace_back(text);
        return "<" + std::string(text) + ">";
    });
    REQUIRE(other.size() == english.size());
    const auto page = [&other](std::string_view path) {
        const auto found = std::ranges::find(other, path, &gs::script::ApiFile::path);
        REQUIRE(found != other.end());
        return found->contents;
    };
    const std::string touch = page("docs/touch.md");
    for (const std::string_view line :
         {"# touch\n\n<Touches on the phone picture.", "## touch.tap(x, y?, durationMs?)\n\n<Taps the picture at a position;",
          "touch.tap(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>",
          "| `x` | `number \\| PicturePoint` | <Where to tap: a ratio of the picture width, or a point.>",
          "| `durationMs` (optional) | `number` | <How long the finger stays down",
          "Returns `Action<void>`. stable since API 1.",
          "- `GS-API-002` when <a position is outside 0 to 1>. <Positions are picture ratios; divide pixels by the "
          "picture size.>",
          "await touch.tap(0.8, 0.9).until("}) {
        CAPTURE(line);
        CHECK(touch.find(line) != std::string::npos);
    }
    CHECK(page("docs/README.md").starts_with("# <GameScrcpy script API>\n\n<Positions are ratios"));
    CHECK(page("docs/README.md").find("- [<Global functions and values>](globals.md)") != std::string::npos);
    CHECK(page("docs/globals.md").starts_with("# <Global functions and values>\n"));

    const std::vector<std::string> texts = gs::script::apiTexts();
    CHECK(texts.size() > 150);
    CHECK(std::set<std::string>(texts.begin(), texts.end()).size() == texts.size());
    CHECK(std::set<std::string>(asked.begin(), asked.end()) == std::set<std::string>(texts.begin(), texts.end()));
    CHECK(std::ranges::find(texts, "Taps the picture at a position; resolves when the finger is lifted, which is at once "
                                   "if the task is cancelled.") != texts.end());
    CHECK(std::ranges::find(texts, "touch") == texts.end());

    const auto source = std::ranges::find(files, std::string("i18n/api-texts.js"), &gs::script::ApiFile::path);
    REQUIRE(source != files.end());
    CHECK(static_cast<std::size_t>(std::ranges::count(source->contents, '\n')) == texts.size());
    CHECK(source->contents.find("QT_TRANSLATE_NOOP(\"ScriptApi\", \"Waits until the next picture frame is shown.\");\n")
          != std::string::npos);
    CHECK(source->contents.find("\\\"KeyW\\\"") != std::string::npos);
}

TEST_CASE("SCR-011: the script host delivers control and state events to the logic's listeners")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
controls.jump.onPress(({ control }) => log.info("pressed", control, Object.keys(controls).join(",")));
controls.jump.onRelease(() => log.info("released"));
vars.aiming.onChange(({ id, value }) => log.info("changed", id, value));
export function aim() {
    vars.aiming.value = true;
    log.info("aiming", vars.aiming.value);
}
)js");
    fixture.host->controlEvent("jump", true);
    REQUIRE(fixture.recorder.waitFor(logged("pressed jump jump,fire,move")).has_value());
    fixture.host->controlEvent("jump", false);
    REQUIRE(fixture.recorder.waitFor(logged("released")).has_value());
    fixture.host->stateEvent("aiming", true);
    REQUIRE(fixture.recorder.waitFor(logged("changed aiming true")).has_value());
    CHECK(fixture.run(script, "aim").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("aiming true")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"state aiming on"});
}

TEST_CASE("issue 34: the logic takes the key of a tap or turbo it listens to, unless the event says consume: false")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
controls.jump.onPress(() => log.info("jump"));
controls.jump.whileHeld(() => {});
controls.move.onPress(() => log.info("move"));
controls.fire.onRelease(() => log.info("fire"), { consume: false });
export function later() {
    const held = controls.fire.whileHeld(() => {});
    held.dispose();
    held.dispose();
    let refused = "";
    try {
        controls.fire.onPress(() => {}, { consume: "yes" });
    } catch (error) {
        refused = error.message;
    }
    log.info("refused", refused.startsWith("GS-API-011"));
}
)js");
    CHECK(fixture.run(script, "later").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("refused true")) == 1);
    CHECK(fixture.services->handed() == std::vector<std::string>{"jump on", "fire on", "fire off"});
    CHECK(fixture.services->events().empty());
}

TEST_CASE("ADR-0091 2026-11-09: a tap control's tap() and tapTimes() hold the finger for its tap length")
{
    Fixture fixture;
    fixture.services->setProps("jump", R"({"inputs":{"press":"Space"},"tapMs":400})");
    const ScriptId script = fixture.load(R"js(
export async function play() {
    let started = time.now();
    await controls.jump.tap();
    log.info("tap long", time.now() - started >= 350);
    started = time.now();
    await controls.jump.tapTimes(2, 10);
    log.info("taps long", time.now() - started >= 750);
}
)js");
    CHECK(fixture.run(script, "play").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("tap long true")) == 1);
    CHECK(fixture.recorder.count(logged("taps long true")) == 1);
    CHECK(fixture.services->events()
          == std::vector<std::string>{"control jump down", "control jump up", "control jump down", "control jump up",
                                      "control jump down", "control jump up"});
}

TEST_CASE("ADR-0091: components are objects of their type with settings, state, events and functions")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const name = (value) => Object.prototype.toString.call(value).slice(8, -1);
log.info(name(controls.jump), name(controls.fire), name(controls.move), controls.jump.key, controls.fire.rateHz,
         controls.jump.tapMs, controls.move.inputs.up, controls.move.radius, controls.fire.firing);
controls.jump.onTouchStart(() => log.info("touch start"));
controls.move.onMoveStart(() => log.info("move start"));
controls.move.onDirectionChange(({ direction }) => log.info("direction", direction));
controls.move.onStop(() => log.info("move stop"));
export function drive() {
    controls.jump.press();
    controls.jump.release();
    controls.jump.reset();
    controls.move.push(90, 0.5);
    controls.move.release();
    controls.jump.hint.hide();
    controls.jump.hint.setText("Jump!");
    controls.jump.hint.moveTo(0.5, 0.25);
    controls.jump.hint.moveTo({ x: 0.75, y: 1 });
    controls.jump.hint.flash();
    try {
        controls.jump.hint.moveTo(1.5, 0.25);
    } catch (error) {
        log.info("moveTo", error.name);
    }
    log.info("touching", controls.jump.touching, controls.move.moving, controls.move.direction);
}
)js");
    fixture.services->setStatus({.touching = true, .stick = {1.0, 0.0}});
    CHECK(fixture.run(script, "drive").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("Tap Turbo Wasd Space 12 40 KeyW 0.12 false")) == 1);
    CHECK(fixture.recorder.count(logged("moveTo RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("touching true true 90")) == 1);
    CHECK(fixture.services->events() ==
          std::vector<std::string>{"control jump down", "control jump up", "act jump reset 0 0",
                                   "act move push 0.5 0", "act move push 0 0",
                                   "app hint {\"id\":\"jump\",\"visible\":false}",
                                   "app hint {\"id\":\"jump\",\"text\":\"Jump!\"}",
                                   "app hint {\"id\":\"jump\",\"x\":0.5,\"y\":0.25}",
                                   "app hint {\"id\":\"jump\",\"x\":0.75,\"y\":1}",
                                   "app hint {\"id\":\"jump\",\"flash\":true}"});
    fixture.host->event("status", "jump", R"({"touching":true,"stick":{"x":0,"y":0},"aiming":false,"walking":false,"cancelled":false})");
    REQUIRE(fixture.recorder.waitFor(logged("touch start")).has_value());
    fixture.host->event("status", "move", R"({"touching":true,"stick":{"x":0,"y":-1},"aiming":false,"walking":false,"cancelled":false})");
    REQUIRE(fixture.recorder.waitFor(logged("move start")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("direction 0")).has_value());
    fixture.host->event("status", "move", R"({"touching":false,"stick":{"x":0,"y":0},"aiming":false,"walking":false,"cancelled":false})");
    REQUIRE(fixture.recorder.waitFor(logged("move stop")).has_value());
}

TEST_CASE("ADR-0091: keys are taken by the script and give press, release, tap, double tap and long press")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
keys.F.onPress(({ key }) => log.info("press", key));
keys.F.onRelease(({ key, heldMs }) => log.info("release", key, heldMs >= 0));
keys.F.onTap(() => log.info("tap"));
keys.F.onDoubleTap(() => log.info("double"));
keys.MouseRight.onLongPress(50, () => log.info("long"));
export function check() {
    let code = "none";
    try {
        keys.Nope.onPress(() => {});
    } catch (error) {
        code = error.message.split(" ")[0];
    }
    log.info(code, keys.F.name, keys.Esc.name, keys["1"].name, keys.GamepadA.name, keys.F.isDown);
}
)js");
    CHECK(fixture.run(script, "check").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-013 KeyF Escape Digit1 Gamepad.A false")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"claim KeyF", "claim Mouse.Right"});
    fixture.host->event("key", "KeyF", "true");
    fixture.host->event("key", "KeyF", "false");
    fixture.host->event("key", "KeyF", "true");
    fixture.host->event("key", "KeyF", "false");
    REQUIRE(fixture.recorder.waitFor(logged("press KeyF")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("release KeyF true")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("tap")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("double")).has_value());
    fixture.host->event("key", "Mouse.Right", "true");
    REQUIRE(fixture.recorder.waitFor(logged("long")).has_value());
    fixture.host->event("key", "Mouse.Right", "false");
}

TEST_CASE("ADR-0091: variables hold typed values; globals, flow helpers and system nodes reach the host")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
vars.round.onChange(({ id, value }) => log.info("round", id, value));
globals.onChange("score", ({ name, value }) => log.info("global", name, value));
export async function run() {
    log.info(vars.round.value, typeof vars.round.value, vars.aiming.value);
    vars.round.value = 3;
    vars.aiming.value = "yes";
    let code = "none";
    try {
        vars.round.value = {};
    } catch (error) {
        code = error.message.split(" ")[0];
    }
    globals.set("score", 7);
    log.info(code, globals.get("score"), globals.get("missing") === undefined);
    await device.home();
    device.type("gg");
    device.launch("com.example.game", { restart: true });
    device.rotate("landscape");
    try {
        device.rotate("sideways");
    } catch (error) {
        log.info("rotate", error.name);
    }
    log.time("t");
    log.info("timer", typeof log.timeEnd("t"), log.timeEnd("t"));
    mapping.on();
    hints.setOpacity(0.25);
    log.info(JSON.stringify(profile.current()), mapping.get("jump").id, mapping.get("nope"),
             mapping.all("wasd").map((c) => c.id).join(","), mapping.all().length);
    const finger = touch.down(point(0.1, 0.2));
    await finger.move(0.3, 0.4);
    finger.up();
    touch.releaseAll();
    log.info(flipFlop("a"), flipFlop("a"), math.clamp(5, 0, 1), math.lerp(0, 10, 0.5), math.angle(point(0.5, 0.5),
             point(0.9, 0.5)), doOnce("x", () => "once"), doOnce("x", () => "again"));
    const door = gate("door", true);
    log.info(door.isOpen, door.run(() => "in"));
    door.open();
    log.info(door.run(() => "in"), await retry(2, (attempt) => { if (attempt < 2) throw new Error("no"); return attempt; }));
    log.info(await waitUntil(() => vars.round.value === 3 && "ready"));
    let ticks = 0;
    const ticker = every(5, () => { ticks += 1; });
    for (let round = 0; round < 200 && ticks <= 2; round += 1) {
        await sleep(10);
    }
    ticker.dispose();
    log.info("ticked", ticks > 2);
    let timedOut = "none";
    try {
        await withTimeout(20, ({ signal }) => new Promise(() => {}));
    } catch (error) {
        timedOut = error.name;
    }
    log.info(timedOut);
}
)js");
    CHECK(fixture.run(script, "run").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("1 number false")) == 1);
    CHECK(fixture.recorder.count(logged("GS-API-010 7 true")) == 1);
    CHECK(fixture.recorder.count(logged("{\"id\":\"p1\",\"name\":\"Shooter\"} jump null move 3")) == 1);
    CHECK(fixture.recorder.count(logged("A B 1 5 90 once undefined")) == 1);
    CHECK(fixture.recorder.count(logged("false undefined")) == 1);
    CHECK(fixture.recorder.count(logged("in 2")) == 1);
    CHECK(fixture.recorder.count(logged("ready")) == 1);
    CHECK(fixture.recorder.count(logged("ticked true")) == 1);
    CHECK(fixture.recorder.count(logged("TimeoutError")) == 1);
    CHECK(fixture.recorder.count(logged("rotate RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("timer number null")) == 1);
    CHECK(fixture.services->events() ==
          std::vector<std::string>{"state round 3.0", "state aiming \"yes\"", "global score 7", "key home down",
                                   "key home up", "type gg",
                                   "app launch {\"package\":\"com.example.game\",\"restart\":true}", "app rotate 2",
                                   "app keymap true",
                                   "app hints {\"opacity\":0.25}", "app profile null", "finger down 0.1 0.2", "finger move 0.3 0.4 0", "lift",
                                   "lift all"});
}

TEST_CASE("INP-012: hints.reveal reaches the phone in order, and a key's press and release turn it on and off")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
keys.G.onPress(() => {
    hints.reveal(true);
    log.info("revealed");
});
keys.G.onRelease(() => {
    hints.reveal(false);
    log.info("back");
});
export function flip() {
    log.info("flip", hints.reveal(true), hints.reveal(false), hints.reveal(1), hints.reveal(""), hints.reveal.length);
}
)js");
    const std::string on = R"(app hints {"reveal":true})";
    const std::string off = R"(app hints {"reveal":false})";
    CHECK(fixture.run(script, "flip").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("flip true true true true 1")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"claim KeyG", on, off, on, off});

    fixture.host->event("key", "KeyG", "true");
    REQUIRE(fixture.recorder.waitFor(logged("revealed")).has_value());
    CHECK(fixture.services->events() == std::vector<std::string>{"claim KeyG", on, off, on, off, on});
    fixture.host->event("key", "KeyG", "false");
    REQUIRE(fixture.recorder.waitFor(logged("back")).has_value());
    CHECK(fixture.services->events() == std::vector<std::string>{"claim KeyG", on, off, on, off, on, off});
}

TEST_CASE("ADR-0091: machine() runs its states in order, follows to.<state>(reason) and reports each state and the end")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function run() {
    let rounds = 0;
    await machine({
        async lobby(to) {
            rounds += 1;
            return to.queue("tapped start");
        },
        async queue(to) {
            return rounds < 2 ? to.lobby("timed out") : to.game();
        },
        async game() {
            log.info("game", rounds);
        },
    }, { name: "farm" });
    let wrong = "";
    try {
        await machine({ async only() { return 5; } });
    } catch (error) {
        wrong = error.name;
    }
    log.info("wrong", wrong);
}
)js");
    CHECK(fixture.run(script, "run").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("game 2")) == 1);
    CHECK(fixture.recorder.count(logged("wrong TypeError")) == 1);
    std::vector<std::string> states;
    for (const std::string& event : fixture.services->events()) {
        if (event.starts_with("app machineState ")) {
            states.push_back(event.substr(17));
        }
    }
    REQUIRE(states.size() == 8);
    const auto has = [&states](std::size_t index, std::string_view text) {
        return states[index].find(text) != std::string::npos;
    };
    CHECK(has(0, R"({"file":"main.js","line":)"));
    CHECK(has(0, R"("name":"farm","state":"lobby","reason":""})"));
    CHECK(has(1, R"("state":"queue","reason":"tapped start"})"));
    CHECK(has(2, R"("state":"lobby","reason":"timed out"})"));
    CHECK(has(4, R"("state":"game","reason":""})"));
    CHECK(has(5, R"("state":"","reason":""})"));
    CHECK(has(6, R"("state":"only")"));
    CHECK(has(7, R"("state":"","reason":""})"));
}

TEST_CASE("ADR-0091: a task's default export extends Task, gets its params and reports progress; plain handlers still run")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export default class Farm extends Task {
    static params = { rounds: param.number(5, { label: "Rounds", min: 1 }), map: param.text("A"), claim: param.toggle(true) };

    async run({ signal }) {
        this.progress("1 / " + this.params.rounds);
        log.info("task", this.params.rounds, this.params.map, this.params.claim, signal.aborted, this instanceof Task);
    }
}
export function plain() {
    log.info("plain", typeof Task, param.number(2).kind, param.toggle(false).value, param.text("x", { label: "L" }).label);
}
export function wrong() {
    let code = "none";
    try {
        param.number("x");
    } catch (error) {
        code = error.message.split(" ")[0];
    }
    log.info(code);
}
)js");
    CHECK(fixture.run(script, "default").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("task 2 A true false true")) == 1);
    CHECK(fixture.run(script, "plain").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("plain function number false L")) == 1);
    CHECK(fixture.run(script, "wrong").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-034")) == 1);
    const std::vector<std::string> events = fixture.services->events();
    CHECK(std::ranges::count(events, std::string("app taskParams null")) == 1);
    CHECK(std::ranges::count(events, std::string("app taskProgress \"1 / 2\"")) == 1);
}

TEST_CASE("ADR-0091: a default export that is not a task class fails like any other handler")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export default class Helper {
    run() {}
}
)js");
    CHECK(fixture.run(script, "default").kind == ReportKind::TaskFailed);
}

TEST_CASE("ADR-0091: a component made in logic extends Component with props, its own events and finger, and setup")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
class HoldToggle extends Component {
    static info = { name: "Hold toggle" };
    static props = { key: prop.key("F"), at: prop.position(point(0.4, 0.6)), count: prop.number(3), partner: prop.component("fire") };
    changed = this.event();
    holding = false;

    setup() {
        log.info("setup", this.name, this.id, this.type, this.props.key.name, this.props.at.x, this.props.count,
                 this.props.partner?.id ?? "none", this.key, this.inputs.key);
        this.props.key.onPress(() => {
            this.holding = !this.holding;
            if (this.holding) {
                this.finger.down(this.props.at);
            } else {
                this.finger.up();
            }
            this.changed.emit(this.holding);
        });
    }
}
const toggle = new HoldToggle({ at: point(0.5, 0.5) });
toggle.changed.on(({ value }) => log.info("changed", value, toggle.finger.isDown));
toggle.onDisabled(() => log.info("off"));
export function check() {
    toggle.enabled = false;
    let action = "none";
    try {
        toggle.press();
    } catch (error) {
        action = error.message.split(" ")[0];
    }
    let base = "none";
    try {
        new Component({});
    } catch (error) {
        base = error.name;
    }
    let builtIn = "none";
    try {
        class MyTap extends controls.jump.constructor {}
        new MyTap({});
    } catch (error) {
        builtIn = error.message.split(" ")[0];
    }
    log.info("checked", action, base, builtIn, toggle instanceof Component, toggle.enabled);
}
)js");
    REQUIRE(fixture.recorder.waitFor(logged("setup Hold toggle HoldToggle-1 HoldToggle KeyF 0.5 3 fire KeyF KeyF")).has_value());
    fixture.host->event("key", "KeyF", "true");
    REQUIRE(fixture.recorder.waitFor(logged("changed true true")).has_value());
    fixture.host->event("key", "KeyF", "false");
    fixture.host->event("key", "KeyF", "true");
    REQUIRE(fixture.recorder.waitFor(logged("changed false false")).has_value());
    CHECK(fixture.run(script, "check").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("checked GS-API-036 TypeError GS-API-036 true false")) == 1);
    REQUIRE(fixture.recorder.waitFor(logged("off")).has_value());
    const std::vector<std::string> events = fixture.services->events();
    CHECK(std::ranges::count(events, std::string("finger down 0.5 0.5")) == 1);
    CHECK(std::ranges::count(events, std::string("lift")) == 1);
}

TEST_CASE("ADR-0091: an instance placed in the profile is built from its class with the profile's keys, values and places")
{
    ScriptFolder classes;
    classes.write("combo/component.ts", R"js(
export default class Combo extends Component {
    static props = { key: prop.key("F"), times: prop.number(3), second: prop.position(), partner: prop.component("jump") };

    setup() {
        log.info("setup", this.id, this.name, this.type, this.props.key.name, this.props.times, this.position.x,
                 this.position.y, this.props.second.x, this.props.second.y, this.props.partner?.id ?? "none");
        this.props.key.onPress(async () => {
            await touch.tap(this.position);
            log.info("tapped", this.id);
        });
    }
}
)js");
    Fixture fixture;
    fixture.services->addControl(
        {"combo", "Combo 1", "component", true,
         R"({"component":"mine/combo","inputs":{"key":"KeyQ"},"props":{"times":2},"at":[0.25,0.75],)"
         R"("points":{"second":[0.5,0.25]}})"});
    fixture.services->addControl({"lost", "Lost", "component", true, R"({"component":"mine/lost"})"});
    fixture.folder.write("main.js", R"js(
export function check() {
    log.info("placed", controls.combo instanceof Component, controls.combo.props.times, controls.lost === null,
             controls.jump.position === null ? "none" : "at");
}
)js");
    gs::script::ScriptSource source = fixture.folder.source();
    source.components.push_back({.ref = "mine/combo", .folder = classes.path() / "combo"});
    ScriptId script = 0;
    const auto report = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    REQUIRE(fixture.recorder.waitFor(logged("setup combo Combo 1 Combo KeyQ 2 0.25 0.75 0.5 0.25 jump")).has_value());
    CHECK(fixture.recorder.count(logged("lost: mine/lost has no default export that extends Component")) == 1);
    CHECK(fixture.run(script, "check").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("placed true 2 true none")) == 1);
    fixture.host->event("key", "KeyQ", "true");
    REQUIRE(fixture.recorder.waitFor(logged("tapped combo")).has_value());
    const std::vector<std::string> events = fixture.services->events();
    CHECK(std::ranges::any_of(events, [](const std::string& event) { return event.starts_with("tap 0.25 0.75"); }));
}

TEST_CASE("ADR-0091: a profile with instances but no logic file of its own still mounts them")
{
    ScriptFolder classes;
    classes.write("ping/component.ts", R"js(
export default class Ping extends Component {
    setup() {
        log.info("ping ready", this.id);
    }
}
)js");
    Fixture fixture;
    fixture.services->addControl({"ping", "Ping", "component", true, R"({"component":"mine/ping"})"});
    gs::script::ScriptSource source = fixture.folder.source("logic.ts");
    source.components.push_back({.ref = "mine/ping", .folder = classes.path() / "ping"});
    const auto report = loadScript(*fixture.host, fixture.recorder, source);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    CHECK(report->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.recorder.waitFor(logged("ping ready ping")).has_value());
}

TEST_CASE("INP-012: the example Show all key hints while held reveals them while its key is held and it is on")
{
    Fixture fixture;
    fixture.services->addControl({"peek", "Show all key hints while held", "component", true,
                                  R"({"component":"examples/show-all-hints","inputs":{"key":"Tab"},"at":[0.9,0.1]})"});
    fixture.services->addControl({"spare", "Spare", "component", true, R"({"component":"examples/show-all-hints"})"});
    fixture.folder.write("main.js", R"js(
export function check() {
    log.info("placed", controls.peek.constructor.name, controls.peek.key, controls.spare.key, controls.peek.enabled);
}
export function off() {
    controls.peek.disable();
}
export function on() {
    controls.peek.enable();
}
)js");
    gs::script::ScriptSource source = fixture.folder.source();
    source.components.push_back({.ref = "examples/show-all-hints",
                                 .folder = std::filesystem::path(GS_EXAMPLE_COMPONENTS_DIR) / "show-all-hints"});
    ScriptId script = 0;
    const auto report = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    const auto settled = [&fixture, script] {
        CHECK(fixture.run(script, "check").kind == ReportKind::TaskFinished);
        return fixture.services->events();
    };
    const std::string on = R"(app hints {"reveal":true})";
    const std::string off = R"(app hints {"reveal":false})";
    CHECK(settled() == std::vector<std::string>{"claim Tab"});
    CHECK(fixture.recorder.count(logged("placed ShowAllHints Tab null true")) == 1);

    fixture.host->event("key", "Tab", "true");
    CHECK(settled() == std::vector<std::string>{"claim Tab", on});
    fixture.host->event("key", "Tab", "false");
    CHECK(settled() == std::vector<std::string>{"claim Tab", on, off});

    fixture.host->event("key", "Tab", "true");
    CHECK(fixture.run(script, "off").kind == ReportKind::TaskFinished);
    CHECK(settled() == std::vector<std::string>{"claim Tab", on, off, on, off});
    fixture.host->event("key", "Tab", "false");
    fixture.host->event("key", "Tab", "true");
    CHECK(settled() == std::vector<std::string>{"claim Tab", on, off, on, off, off});

    fixture.host->event("key", "Tab", "false");
    CHECK(fixture.run(script, "on").kind == ReportKind::TaskFinished);
    fixture.host->event("key", "Tab", "true");
    CHECK(settled() == std::vector<std::string>{"claim Tab", on, off, on, off, off, off, on});
    CHECK(fixture.recorder.count(kindIs(ReportKind::UnhandledRejection)) == 0);
}

TEST_CASE("ADR-0091: a class that extends a built-in component is placed as it and takes its events as well")
{
    ScriptFolder classes;
    classes.write("quick-scope/component.ts", R"js(
export default class QuickScope extends Hold {
    static props = { boost: prop.key("B"), times: prop.number(3), press: prop.key("P") };
    count = 0;

    setup() {
        log.info("setup", this.id, this.type, this.name, this.props.boost.name, this.props.times, this.props.press,
                 this.key, this.inputs.boost, this instanceof Hold, String(this), this.holdMs ?? "none");
    }

    onPressed(event) {
        super.onPressed();
        super.onPressed(event);
        this.count += 1;
        log.info("pressed", event.control, this.count, this.holding);
    }
}
)js");
    Fixture fixture;
    fixture.services->addControl(
        {"scope", "Scope", "hold", true,
         R"({"inputs":{"press":"Mouse.Right","boost":"KeyG"},"component":"mine/quick-scope","props":{"times":2}})"});
    fixture.folder.write("main.js", R"js(
controls.scope.onPressed(() => log.info("listener"));
export function check() {
    let builtIn = "none";
    try {
        new Hold();
    } catch (error) {
        builtIn = error.message.split(" ")[0];
    }
    log.info("placed", controls.scope.constructor.name, controls.scope instanceof Hold, mapping.all("hold").length,
             mapping.all(Hold).length, controls.scope.props.times, typeof controls.scope.onReleased(), builtIn);
}
)js");
    gs::script::ScriptSource source = fixture.folder.source();
    source.components.push_back({.ref = "mine/quick-scope", .folder = classes.path() / "quick-scope"});
    ScriptId script = 0;
    const auto report = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    REQUIRE(fixture.recorder.waitFor(logged("setup scope hold Scope KeyG 2 null Mouse.Right KeyG true [object Hold] none"))
                .has_value());
    CHECK(fixture.recorder.count(logged("scope: the key property press has the name of a key of Hold and is left out")) == 1);
    fixture.services->setStatus({.touching = true});
    fixture.host->event("status", "scope",
                        R"({"touching":true,"stick":{"x":0,"y":0},"aiming":false,"walking":false,"cancelled":false})");
    REQUIRE(fixture.recorder.waitFor(logged("pressed scope 1 true")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("listener")).has_value());
    CHECK(fixture.run(script, "check").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("placed QuickScope true 1 1 2 undefined GS-API-036")) == 1);
}

TEST_CASE("ADR-0091: a component class can extend another of my components through its folder, and go nowhere else")
{
    ScriptFolder classes;
    classes.write("base/component.ts", R"js(
export default class Base extends Component {
    static props = { key: prop.key("F"), times: prop.number(2) };

    setup() {
        log.info("base setup", this.id, this.props.key.name, this.props.times, this.props.extra);
    }
}
)js");
    classes.write("base/shared.ts", "export const word = \"shared\";\n");
    classes.write("plus/component.ts", R"js(
import Base from "../base/component.ts";
import { word } from "../base/shared.ts";

export default class Plus extends Base {
    static props = { extra: prop.text("more"), times: prop.number(5) };

    setup() {
        super.setup();
        log.info("plus setup", word, Object.keys(this.props).join(","));
    }
}
)js");
    classes.write("escape/component.ts", R"js(
import Other from "../missing/component.ts";
export default class Escape extends Other {}
)js");
    Fixture fixture;
    fixture.services->addControl(
        {"plus", "Plus", "component", true, R"({"component":"mine/plus","inputs":{"key":"KeyQ"}})"});
    gs::script::ScriptSource source = fixture.folder.source("logic.ts");
    source.components.push_back({.ref = "mine/plus", .folder = classes.path() / "plus"});
    source.components.push_back({.ref = "mine/base", .folder = classes.path() / "base", .placed = false});
    const auto report = loadScript(*fixture.host, fixture.recorder, source);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    REQUIRE(fixture.recorder.waitFor(logged("base setup plus KeyQ 5 more")).has_value());
    REQUIRE(fixture.recorder.waitFor(logged("plus setup shared key,times,extra")).has_value());

    Fixture escaping;
    escaping.services->addControl({"escape", "Escape", "component", true, R"({"component":"mine/escape"})"});
    gs::script::ScriptSource away = escaping.folder.source("logic.ts");
    away.components.push_back({.ref = "mine/escape", .folder = classes.path() / "escape"});
    away.components.push_back({.ref = "mine/base", .folder = classes.path() / "base", .placed = false});
    const auto failed = loadScript(*escaping.host, escaping.recorder, away);
    REQUIRE(failed.has_value());
    INFO(gs::script::describe(*failed));
    CHECK(failed->kind == ReportKind::ScriptFailed);
    CHECK(failed->message.find("leaves the component folder") != std::string::npos);
}

TEST_CASE("ADR-0091: a group's fields take its child components from the profile in the order the class declares them")
{
    ScriptFolder classes;
    classes.write("volley/component.ts", R"js(
export default class Volley extends Component {
    cancel = this.child(Tap);
    skills = [this.child(Skill), this.child(Skill)];
    spare = this.child(Hold);

    setup() {
        log.info("children", this.cancel?.id, this.skills.map((skill) => skill?.id ?? "none").join(","), this.spare?.id,
                 this.child(Tap));
    }
}
)js");
    Fixture fixture;
    fixture.services->addControl({"volley", "Volley", "component", true, R"({"component":"mine/volley"})"});
    fixture.services->addControl({"volley-skills-1", "Skill B", "skill", true, R"({"parent":"volley","child":"skills.1"})"});
    fixture.services->addControl({"volley-cancel", "Cancel", "tap", true, R"({"parent":"volley","child":"cancel"})"});
    fixture.services->addControl({"volley-skills-0", "Skill A", "skill", true, R"({"parent":"volley","child":"skills.0"})"});
    fixture.services->addControl({"stray", "Stray", "hold", true, R"({"parent":"volley","child":"spare"})"});
    gs::script::ScriptSource source = fixture.folder.source("logic.ts");
    source.components.push_back({.ref = "mine/volley",
                                 .folder = classes.path() / "volley",
                                 .children = {"cancel", "skills.0", "skills.1", "spare"}});
    const auto report = loadScript(*fixture.host, fixture.recorder, source);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    REQUIRE(fixture.recorder.waitFor(logged("children volley-cancel volley-skills-0,volley-skills-1 stray null"))
                .has_value());
}

TEST_CASE("ADR-0091: components of different kinds that implement one interface are found together")
{
    ScriptFolder classes;
    classes.write("gun/component.ts", R"js(
const Fire = defineInterface("fire");

export default class Gun extends Tap {
    static implements = [Fire];

    fire() {
        log.info("gun fires", this.id);
    }
}
)js");
    classes.write("bow/component.ts", R"js(
export default class Bow extends Component {
    static implements = [defineInterface("fire")];
    static props = { partner: prop.component(Tap), other: prop.component(Hold), shooter: prop.component(defineInterface("fire")) };

    fire() {
        log.info("bow fires", this.id, this.props.partner?.id, this.props.other, this.props.shooter?.id);
    }
}
)js");
    Fixture fixture;
    fixture.services->addControl({"gun", "Gun", "tap", true, R"({"inputs":{"press":"KeyG"},"component":"mine/gun"})"});
    fixture.services->addControl(
        {"bow", "Bow", "component", true,
         R"({"component":"mine/bow","props":{"partner":"jump","other":"jump","shooter":"gun"}})"});
    fixture.folder.write("main.js", R"js(
const Fire = defineInterface("fire");
export function shoot() {
    for (const shooter of mapping.all(Fire)) {
        shooter.fire();
    }
    let code = "none";
    try {
        defineInterface("");
    } catch (error) {
        code = error.message.split(" ")[0];
    }
    log.info("found", Fire.all().length, mapping.all(Hold).length, mapping.all(Tap).length, Fire.name,
             Fire === defineInterface("fire"), String(Fire), code);
}
)js");
    gs::script::ScriptSource source = fixture.folder.source();
    source.components.push_back({.ref = "mine/gun", .folder = classes.path() / "gun"});
    source.components.push_back({.ref = "mine/bow", .folder = classes.path() / "bow"});
    ScriptId script = 0;
    const auto report = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.run(script, "shoot").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("gun fires gun")) == 1);
    CHECK(fixture.recorder.count(logged("bow fires bow jump null gun")) == 1);
    CHECK(fixture.recorder.count(logged("found 2 0 2 fire true [object Interface] GS-API-011")) == 1);
}

TEST_CASE("ADR-0091: touch.pinch moves two fingers apart around a point and lifts both")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function zoom() {
    await touch.pinch(point(0.5, 0.5), 0.1, 0.4, 300);
    let code = "none";
    try {
        touch.pinch(point(0.5, 0.5), 2, 0.4);
    } catch (error) {
        code = error.message.split(" ")[0];
    }
    log.info("pinched", code);
}
)js");
    CHECK(fixture.run(script, "zoom").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("pinched GS-API-002")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"finger down 0.45 0.5", "finger down 0.55 0.5",
                                                                  "finger move 0.3 0.5 300", "finger move 0.7 0.5 300",
                                                                  "lift", "lift"});
}

TEST_CASE("ADR-0091: clipboard.read and clipboard.write ask the host for the phone's clipboard and resolve with its answers")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.name + " " + error.message.split(" ")[0];
    }
};
export async function clip() {
    log.info("read", JSON.stringify(await clipboard.read()));
    log.info("empty", JSON.stringify(await clipboard.read()));
    log.info("unshared", JSON.stringify(await clipboard.read()));
    log.info("wrote", await clipboard.write("from logic"));
    log.info("refused", await clipboard.write("again"));
    log.info(codeOf(() => clipboard.write(3)), codeOf(() => clipboard.write()),
             codeOf(() => clipboard.write("\u597d".repeat(87334))), Object.isFrozen(clipboard));
}
)js");
    const TaskId task = fixture.host->start(script, "clip");
    const std::vector<std::string> answers{"\"on the phone\"", "\"\"", "null", "true", "false"};
    for (std::size_t i = 0; i < answers.size(); ++i) {
        CAPTURE(i);
        const std::optional<std::uint64_t> request = fixture.services->waitForRequest(i);
        REQUIRE(request.has_value());
        fixture.host->answer(*request, answers[i]);
    }
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    INFO(gs::script::describe(ended->report));
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("read \"on the phone\"")) == 1);
    CHECK(fixture.recorder.count(logged("empty \"\"")) == 1);
    CHECK(fixture.recorder.count(logged("unshared null")) == 1);
    CHECK(fixture.recorder.count(logged("wrote true")) == 1);
    CHECK(fixture.recorder.count(logged("refused false")) == 1);
    CHECK(fixture.recorder.count(logged("TypeError GS-API-011 TypeError GS-API-011 RangeError GS-API-017 true")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"clipboard read", "clipboard read", "clipboard read",
                                                                  "clipboard write from logic",
                                                                  "clipboard write again"});
}

TEST_CASE("ADR-0091: vision.pickColor and matchColor ask for one pixel, resolve with the answer and check their arguments")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
export async function look() {
    const color = vision.pickColor(point(0.25, 0.5));
    const same = vision.matchColor({ x: 1, y: 0 }, "#00ff00", { tolerance: 0.2, timeoutMs: 500 });
    log.info("asked");
    log.info("answered", await color, await same);
}
export async function wait() {
    log.info("waiting");
    await vision.pickColor(point(0.5, 0.5), { timeoutMs: 10000 });
}
export function wrong() {
    log.info(codeOf(() => vision.pickColor(0.5)), codeOf(() => vision.pickColor({ x: 2, y: 0 })),
             codeOf(() => vision.pickColor({ x: 0.5 })), codeOf(() => vision.pickColor(point(0.5, 0.5), 3)),
             codeOf(() => vision.pickColor(point(0.5, 0.5), { timeoutMs: 0 })),
             codeOf(() => vision.matchColor(point(0.5, 0.5))), codeOf(() => vision.matchColor(point(0.5, 0.5), "#12345")),
             codeOf(() => vision.matchColor(point(0.5, 0.5), "#123456", { tolerance: 2 })),
             codeOf(() => vision.matchColor(point(0.5, 0.5), "gold")));
}
)js");
    const TaskId task = fixture.host->start(script, "look");
    REQUIRE(fixture.recorder.waitFor(logged("asked", task)).has_value());
    const std::vector<std::uint64_t> picks = fixture.services->picks();
    REQUIRE(picks.size() == 2);
    fixture.host->answer(picks[0], "\"#123456\"");
    fixture.host->answer(picks[1], "true");
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("answered #123456 true")) == 1);
    CHECK(fixture.services->events()
          == std::vector<std::string>{"pickColor 0.25 0.5 2000", "matchColor 1 0 #00ff00 00FF00 0.2 500"});

    const TaskId waiting = fixture.host->start(script, "wait");
    REQUIRE(fixture.recorder.waitFor(logged("waiting", waiting)).has_value());
    fixture.host->cancel(waiting);
    const auto cancelled = fixture.recorder.waitFor(taskEnded(waiting));
    REQUIRE(cancelled.has_value());
    CHECK(cancelled->report.kind == ReportKind::TaskCancelled);
    CHECK(fixture.services->events()
          == std::vector<std::string>{"pickColor 0.25 0.5 2000", "matchColor 1 0 #00ff00 00FF00 0.2 500",
                                      "pickColor 0.5 0.5 10000", "cancel pick"});

    CHECK(fixture.run(script, "wrong").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-002 GS-API-002 GS-API-002 GS-API-011 GS-API-003 GS-API-019 GS-API-019 "
                                        "GS-API-017 GS-API-018"))
          == 1);
    CHECK(fixture.services->picks().size() == 3);
}

TEST_CASE("ADR-0091: ui.progress shows or clears one card, and ui.notify says whether it was shown")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
export function show() {
    ui.progress("Rounds", 0.25);
    ui.progress(null);
    log.info("notified", ui.notify("Done", "All rounds finished"));
    log.info(codeOf(() => ui.progress("Rounds", 2)), codeOf(() => ui.progress("Rounds")),
             codeOf(() => ui.progress("Rounds", Number.NaN)));
}
)js");
    CHECK(fixture.run(script, "show").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("notified true")) == 1);
    CHECK(fixture.recorder.count(logged("GS-API-017 GS-API-017 GS-API-017")) == 1);
    CHECK(fixture.services->events()
          == std::vector<std::string>{R"(app progress {"title":"Rounds","value":0.25})", "app progress null",
                                      R"(app notify {"title":"Done","text":"All rounds finished"})"});
}

TEST_CASE("ADR-0091: vision.capture takes an image that find, ui.image and save use; vision.changed compares with the "
          "last look")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
export async function look() {
    const shot = vision.capture({ x: 0.1, y: 0.2, width: 0.3, height: 0.1 });
    log.info("asked");
    const image = await shot;
    log.info("captured", image.width, image.height, image.region.x, Object.keys(image).join(","), String(image));
    ui.image(image);
    ui.image(image, { x: 0.5, y: 0.5, width: 0.2, height: 0.2 }, { color: "good" });
    await image.save("Boss");
    const busy = await image.save("Busy").then(() => "none", (error) => error.message.split(" ")[0]);
    log.info(codeOf(() => image.save("")), busy, codeOf(() => ui.image("res/boss.png")),
             codeOf(() => ui.image(42)), codeOf(() => vision.capture({ x: 0.9, y: 0, width: 0.5, height: 1 })));
    const found = vision.find(image, { threshold: 0.7 });
    log.info("finding");
    log.info("found", (await found).score);
}
export async function nothing() {
    const shot = vision.capture();
    log.info("asked nothing");
    log.info("none", await shot);
}
export async function changes() {
    const first = vision.changed({ x: 0, y: 0, width: 0.5, height: 0.5 }, { threshold: 0.1 });
    log.info("looking");
    log.info("changed", await first, codeOf(() => vision.changed(undefined, { threshold: 2 })));
}
)js");
    const auto without = [](std::vector<std::string> events) {
        std::erase(events, std::string("erase"));
        return events;
    };
    const TaskId task = fixture.host->start(script, "look");
    const std::optional<std::uint64_t> capture = fixture.services->waitForRequest(0);
    REQUIRE(capture.has_value());
    fixture.host->answer(
        *capture, R"({"capture":3,"x":0.1,"y":0.2,"width":0.3,"height":0.1,"pixelWidth":324,"pixelHeight":240})");
    const std::optional<std::uint64_t> saved = fixture.services->waitForRequest(1);
    REQUIRE(saved.has_value());
    fixture.host->answer(*saved, "null");
    const std::optional<std::uint64_t> busy = fixture.services->waitForRequest(2);
    REQUIRE(busy.has_value());
    fixture.host->refuse(*busy, "GS-API-038 the image cannot be saved: another program is using res/");
    const std::optional<std::uint64_t> find = fixture.services->waitForRequest(3);
    REQUIRE(find.has_value());
    fixture.host->answer(*find, R"({"x":0.25,"y":0.25,"width":0.3,"height":0.1,"score":0.95})");
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    INFO(gs::script::describe(ended->report));
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("captured 324 240 0.1 region,width,height,save [object CapturedImage]")) == 1);
    CHECK(fixture.recorder.count(logged("GS-API-038 GS-API-038 GS-API-002 GS-API-015 GS-API-002")) == 1);
    CHECK(fixture.recorder.count(logged("found 0.95")) == 1);
    CHECK(without(fixture.services->events())
          == std::vector<std::string>{"capture 0.1 0.2 0.3 0.1 2000", "draw image capture 3 0.1 0.2 0.3 0.1 accent",
                                      "draw image capture 3 0.5 0.5 0.2 0.2 good", "save 3 Boss", "save 3 Busy",
                                      "find captured image 3 3 0.7 color"});

    const TaskId empty = fixture.host->start(script, "nothing");
    const std::optional<std::uint64_t> whole = fixture.services->waitForRequest(4);
    REQUIRE(whole.has_value());
    fixture.host->answer(*whole, "null");
    REQUIRE(fixture.recorder.waitFor(taskEnded(empty)).has_value());
    CHECK(fixture.recorder.count(logged("none null")) == 1);

    const TaskId looking = fixture.host->start(script, "changes");
    const std::optional<std::uint64_t> change = fixture.services->waitForRequest(5);
    REQUIRE(change.has_value());
    fixture.host->answer(*change, "true");
    REQUIRE(fixture.recorder.waitFor(taskEnded(looking)).has_value());
    CHECK(fixture.recorder.count(logged("changed true GS-API-017")) == 1);
    const std::vector<std::string> events = without(fixture.services->events());
    REQUIRE(events.size() == 8);
    CHECK(events[6] == "capture 0 0 1 1 2000");
    CHECK(events[7] == "changed changed 0 0 0.5 0.5 0.5 0.1");
}

TEST_CASE("ADR-0093: vision.readText and findText ask for text with their region, language and timeout, resolve "
          "with the answer and check their arguments")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
export async function read() {
    const all = vision.readText();
    const part = vision.readText({ region: { x: 0.1, y: 0.2, width: 0.5, height: 0.25 }, language: "zh-Hans",
                                   timeoutMs: 500 });
    log.info("asked");
    const whole = await all;
    log.info("read", whole.text.split("\n").length, whole.lines[1].text, whole.lines[1].y, whole.language);
    log.info("part", await part);
}
export async function find() {
    const ok = vision.findText(" O K ", { language: "en" });
    log.info("finding");
    const found = await ok;
    log.info("found", found.x, found.score);
    const missing = await vision.findText("设置").then(() => "none", (error) => error.message.split(" ")[0]);
    log.info("missing", missing);
}
export function wrong() {
    log.info(codeOf(() => vision.findText("")), codeOf(() => vision.findText("   ")),
             codeOf(() => vision.findText(42)), codeOf(() => vision.findText("x".repeat(201))),
             codeOf(() => vision.readText(3)), codeOf(() => vision.readText({ language: "zh_CN" })),
             codeOf(() => vision.readText({ language: 7 })),
             codeOf(() => vision.readText({ region: { x: 0.9, y: 0, width: 0.5, height: 1 } })),
             codeOf(() => vision.readText({ timeoutMs: 0 })),
             codeOf(() => vision.findText("OK", { region: "Nowhere" })), codeOf(() => vision.findText("x".repeat(200))));
}
)js");
    const TaskId reading = fixture.host->start(script, "read");
    const std::optional<std::uint64_t> all = fixture.services->waitForRequest(0);
    const std::optional<std::uint64_t> part = fixture.services->waitForRequest(1);
    REQUIRE(all.has_value());
    REQUIRE(part.has_value());
    fixture.host->answer(*all, R"({"text":"Battery\nSearch settings","lines":[)"
                               R"({"text":"Battery","x":0.2,"y":0.1,"width":0.2,"height":0.03},)"
                               R"({"text":"Search settings","x":0.3,"y":0.2,"width":0.4,"height":0.03}],)"
                               R"("language":"en-US"})");
    fixture.host->answer(*part, "null");
    const auto read = fixture.recorder.waitFor(taskEnded(reading));
    REQUIRE(read.has_value());
    INFO(gs::script::describe(read->report));
    CHECK(read->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("read 2 Search settings 0.2 en-US")) == 1);
    CHECK(fixture.recorder.count(logged("part null")) == 1);

    const TaskId finding = fixture.host->start(script, "find");
    const std::optional<std::uint64_t> ok = fixture.services->waitForRequest(2);
    REQUIRE(ok.has_value());
    fixture.host->answer(*ok, R"({"x":0.5,"y":0.25,"width":0.1,"height":0.05,"score":1})");
    const std::optional<std::uint64_t> missing = fixture.services->waitForRequest(3);
    REQUIRE(missing.has_value());
    fixture.host->refuse(*missing, "GS-API-039 this computer has no text recognition for \"\"");
    const auto found = fixture.recorder.waitFor(taskEnded(finding));
    REQUIRE(found.has_value());
    CHECK(found->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("found 0.5 1")) == 1);
    CHECK(fixture.recorder.count(logged("missing GS-API-039")) == 1);

    const std::vector<std::string> asked{"readText [] 0 0 1 1 [] 2000", "readText [] 0.1 0.2 0.5 0.25 [zh-Hans] 500",
                                         "findText [ O K ] 0 0 1 1 [en] 2000", "findText [设置] 0 0 1 1 [] 2000"};
    CHECK(fixture.services->events() == asked);

    const TaskId wrong = fixture.host->start(script, "wrong");
    const auto checked = fixture.recorder.waitFor(taskEnded(wrong));
    REQUIRE(checked.has_value());
    CHECK(checked->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-011 GS-API-011 GS-API-011 GS-API-011 GS-API-011 GS-API-011 GS-API-011 "
                                        "GS-API-002 GS-API-003 GS-API-018 none"))
          == 1);
    const std::vector<std::string> events = fixture.services->events();
    REQUIRE(events.size() >= 5);
    CHECK(events[4] == std::format("findText [{}] 0 0 1 1 [] 2000", std::string(200, 'x')));
}

TEST_CASE("ADR-0091: vision.onChange keeps looking and starts a task each time the picture changed")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
let changes = 0;
vision.onChange({ x: 0, y: 0, width: 1, height: 0.5 }, () => {
    changes += 1;
    log.info("changed", changes);
}, { intervalMs: 50, threshold: 0.2 });
export function idle() {}
)js");
    static_cast<void>(script);
    const std::optional<std::uint64_t> first = fixture.services->waitForRequest(0);
    REQUIRE(first.has_value());
    fixture.host->answer(*first, "false");
    const std::optional<std::uint64_t> second = fixture.services->waitForRequest(1);
    REQUIRE(second.has_value());
    fixture.host->answer(*second, "true");
    REQUIRE(fixture.recorder.waitFor(logged("changed 1")).has_value());
    REQUIRE(fixture.services->waitForRequest(2).has_value());
    CHECK(fixture.recorder.count(logged("changed 1")) == 1);
    const std::vector<std::string> events = fixture.services->events();
    REQUIRE(events.size() >= 3);
    CHECK(events[0] == "changed onChange 1 1 0.2");
    CHECK(events[1] == "changed onChange 1 1 0.2");
    CHECK(events[2] == "changed onChange 1 1 0.2");
}

TEST_CASE("ADR-0091: components change their place, rate, length and radius until put back, and profile.save asks "
          "the host")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
class Mine extends Component {}
export async function tune() {
    controls.fire.setRate(12);
    log.info("rate", controls.fire.rateHz, controls.fire.position.x, controls.fire.position.y);
    controls.fire.moveTo(0.25, 0.75);
    controls.fire.moveBy(0.25, -0.25);
    controls.fire.resetRate();
    controls.fire.resetProps();
    controls.move.setRadius(0.2);
    controls.jump.setDuration(60);
    const mine = new Mine();
    log.info(codeOf(() => controls.fire.setRate(31)), codeOf(() => controls.move.setRadius(0)),
             codeOf(() => controls.fire.moveTo(1.5, 0)), codeOf(() => controls.jump.setDuration(-1)),
             codeOf(() => mine.moveTo(0.5, 0.5)));
    const saved = profile.save();
    log.info("asked");
    log.info("saved", await saved);
}
)js");
    const TaskId task = fixture.host->start(script, "tune");
    const std::optional<std::uint64_t> request = fixture.services->waitForRequest(0);
    REQUIRE(request.has_value());
    fixture.host->answer(*request, "true");
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    INFO(gs::script::describe(ended->report));
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("rate 12 0.25 0.75")) == 1);
    CHECK(fixture.recorder.count(logged("GS-API-017 GS-API-017 GS-API-002 GS-API-017 GS-API-036")) == 1);
    CHECK(fixture.recorder.count(logged("saved true")) == 1);
    CHECK(fixture.services->events()
          == std::vector<std::string>{R"(tune fire {"rateHz":12})", R"(tune fire {"at":[0.25,0.75]})",
                                      R"(app hint {"id":"fire","x":0.25,"y":0.75})", R"(tune fire {"at":[0.5,0.5]})",
                                      R"(app hint {"id":"fire","x":0.25,"y":0.75})", R"(tune fire {"reset":"rate"})",
                                      R"(tune fire {"reset":"all"})", R"(app hint {"id":"fire","x":0.25,"y":0.75})",
                                      R"(tune move {"radius":0.2})", R"(tune jump {"tapMs":60})", "save profile"});
}

TEST_CASE("ADR-0091: window.focused, onFocus and onBlur follow the app's word on the mirror window")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
let blurs = 0;
window.onFocus(() => log.info("focus", window.focused()));
const blurred = window.onBlur(() => {
    blurs += 1;
    log.info("blur", blurs, window.focused());
});
log.info("start", window.focused(), Object.isFrozen(window), Object.keys(window).join(","));
export function quiet() {
    blurred.dispose();
}
)js");
    CHECK(fixture.recorder.count(logged("start false true focused,onFocus,onBlur")) == 1);
    fixture.services->setFocused(true);
    fixture.host->event("app", "focus", "{}");
    REQUIRE(fixture.recorder.waitFor(logged("focus true")).has_value());
    fixture.services->setFocused(false);
    fixture.host->event("app", "blur", "{}");
    REQUIRE(fixture.recorder.waitFor(logged("blur 1 false")).has_value());
    CHECK(fixture.run(script, "quiet").kind == ReportKind::TaskFinished);
    fixture.host->event("app", "blur", "{}");
    fixture.services->setFocused(true);
    fixture.host->event("app", "focus", "{}");
    REQUIRE(fixture.recorder.waitForCount(logged("focus true"), 2));
    CHECK(fixture.recorder.count(logged("blur 2 false")) == 0);
    CHECK(fixture.recorder.count(logged("focus false")) == 0);
}

TEST_CASE("ADR-0104: device.onInputBox hears the companion's input box open, change and close")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
let boxes = 0;
const watching = device.onInputBox(({ shown, inputType, action, multiLine, signal }) => {
    boxes += 1;
    log.info("box", boxes, shown, inputType, action, multiLine, signal.aborted);
});
log.info("listening", typeof watching.dispose);
export function quiet() {
    watching.dispose();
}
)js");
    CHECK(fixture.recorder.count(logged("listening function")) == 1);
    fixture.host->event("app", "inputBox", R"({"shown":true,"inputType":16385,"action":"send","multiLine":false})");
    REQUIRE(fixture.recorder.waitFor(logged("box 1 true 16385 send false false")).has_value());
    fixture.host->event("app", "inputBox", R"({"shown":true,"inputType":131073,"action":"unspecified","multiLine":true})");
    REQUIRE(fixture.recorder.waitFor(logged("box 2 true 131073 unspecified true false")).has_value());
    fixture.host->event("app", "inputBox", R"({"shown":false,"inputType":0,"action":"unspecified","multiLine":false})");
    REQUIRE(fixture.recorder.waitFor(logged("box 3 false 0 unspecified false false")).has_value());
    CHECK(fixture.run(script, "quiet").kind == ReportKind::TaskFinished);
    fixture.host->event("app", "inputBox", R"({"shown":true,"inputType":1,"action":"done","multiLine":false})");
    CHECK_FALSE(fixture.recorder.waitFor(logged("box 4 true 1 done false false"), 0, std::chrono::milliseconds(300))
                    .has_value());
}

TEST_CASE("ADR-0091: logic, the modules it imports and components import the script API from \"gamescrcpy\"")
{
    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const auto typings = std::ranges::find(files, std::string("types/gamescrcpy.d.ts"), &gs::script::ApiFile::path);
    REQUIRE(typings != files.end());
    for (const std::string_view line :
         {"declare namespace window {\n", "declare module \"gamescrcpy\" {\n", "    export import sleep = globalThis.sleep;\n",
          "    export import Component = globalThis.Component;\n", "    export import window = globalThis.window;\n"}) {
        CAPTURE(line);
        CHECK(typings->contents.find(line) != std::string::npos);
    }

    ScriptFolder classes;
    classes.write("ping/component.ts", R"js(
import { Component, log as say } from "gamescrcpy";

export default class Ping extends Component {
    setup() {
        say.info("ping ready", this.id, this instanceof Component);
    }
}
)js");
    Fixture fixture;
    fixture.services->addControl({"ping", "Ping", "component", true, R"({"component":"mine/ping"})"});
    fixture.folder.write("helper.js", R"js(
import { touch, sleep } from "gamescrcpy";

export async function tapTwice(x, y) {
    await touch.tap(x, y);
    await sleep(5);
    await touch.tap(x, y);
}
)js");
    fixture.folder.write("main.js", R"js(
import { log, point as at, Component as Base, window as mirror } from "gamescrcpy";
import * as api from "gamescrcpy";
import { tapTwice } from "./helper.js";

export async function play() {
    await tapTwice(at(0.5, 0.25).x, 0.25);
    log.info("same", log === globalThis.log, Base === Component, mirror === window, api.controls === controls,
             api.Task === Task, typeof api.sleep, Object.isFrozen(api.touch));
}
)js");
    gs::script::ScriptSource source = fixture.folder.source();
    source.components.push_back({.ref = "mine/ping", .folder = classes.path() / "ping"});
    ScriptId script = 0;
    const auto report = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    REQUIRE(fixture.recorder.waitFor(logged("ping ready ping true")).has_value());
    CHECK(fixture.run(script, "play").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("same true true true true true function true")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"tap 0.5 0.25 60", "tap 0.5 0.25 60"});

    const struct {
        const char* source;
        const char* reason;
    } refused[] = {
        {"import { nope } from \"gamescrcpy\";\n", "Could not find export 'nope' in module 'gamescrcpy'"},
        {"import { log } from \"GameScrcpy\";\n", "only relative imports"},
        {"import { log } from \"gamescrcpy/log\";\n", "only relative imports"},
    };
    for (const auto& entry : refused) {
        CAPTURE(entry.source);
        fixture.folder.write("main.js", entry.source);
        const auto failed = loadScript(*fixture.host, fixture.recorder, fixture.folder.source());
        REQUIRE(failed.has_value());
        INFO(gs::script::describe(*failed));
        CHECK(failed->kind == ReportKind::ScriptFailed);
        CHECK(failed->message.find(entry.reason) != std::string::npos);
    }
}

TEST_CASE("ADR-0091: ui.mark marks a point with the color, label and duration of its options until it is disposed")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
export function mark() {
    const spot = ui.mark(0.25, 0.75, { color: "good", label: "here", durationMs: 1000 });
    const plain = ui.mark(0.5, 0.5);
    log.info("marks", codeOf(() => ui.mark(1.5, 0)), codeOf(() => ui.mark(0, 0, { color: "pink" })),
             codeOf(() => ui.mark(0, 0, { label: 3 })), codeOf(() => ui.mark(0, 0, { durationMs: 0 })),
             codeOf(() => ui.mark(0, 0, 3)));
    spot.dispose();
    plain.dispose();
}
)js");
    CHECK(fixture.run(script, "mark").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("marks GS-API-002 GS-API-014 GS-API-011 GS-API-003 GS-API-011")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"draw mark 0.25 0.75 good here",
                                                                  "draw mark 0.5 0.5 accent", "erase", "erase"});
}

TEST_CASE("ADR-0094: a direction wheel, a multi-press and a temporary cursor are objects of their own classes")
{
    Fixture fixture;
    fixture.services->addControl({"wheel", "Wheel", "dial", true, R"({"inputs":{"press":"KeyQ"},"cells":4,"firstDeg":0})"});
    fixture.services->addControl(
        {"pair", "Pair", "multiPress", true, R"({"inputs":{"press":"KeyE"},"points":[[0.1,0],[0,0.1]]})"});
    fixture.services->addControl({"pointer", "Pointer", "tempCursor", true, R"({"inputs":{"press":"AltLeft"}})"});
    const ScriptId script = fixture.load(R"js(
controls.wheel.onOpen(() => log.info("open"));
controls.wheel.onChoose(({ cell }) => log.info("chose", cell));
controls.pointer.onShow(() => log.info("show"));
controls.pointer.onHide(() => log.info("hide"));
export async function check() {
    log.info(String(controls.wheel), controls.wheel instanceof Dial, controls.wheel.cells, controls.wheel.cell,
             controls.pair instanceof MultiPress, controls.pair.places, controls.pointer instanceof TempCursor,
             controls.pointer.showing);
    let code = "none";
    try {
        controls.wheel.choose(4);
    } catch (error) {
        code = error.message.split(" ")[0];
    }
    log.info("code", code);
    await controls.wheel.choose(0);
    log.info("chosen");
}
)js");
    fixture.services->setStatus({.touching = false, .cell = 2});
    CHECK(fixture.run(script, "check").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("[object Dial] true 4 2 true 3 true false")) == 1);
    CHECK(fixture.recorder.count(logged("code GS-API-017")) == 1);
    CHECK(fixture.recorder.count(logged("chosen")) == 1);
    CHECK(fixture.services->events()
          == std::vector<std::string>{"control wheel down", "act wheel turn 0 -100", "control wheel up"});

    const auto status = [&fixture](const char* id, bool touching, int cell) {
        fixture.host->event("status", id,
                            std::format(R"({{"touching":{},"stick":{{"x":0,"y":0}},"aiming":false,"walking":false,)"
                                        R"("cancelled":false,"cell":{}}})",
                                        touching, cell));
    };
    status("wheel", true, -1);
    REQUIRE(fixture.recorder.waitFor(logged("open")).has_value());
    status("wheel", true, 3);
    status("wheel", false, -1);
    REQUIRE(fixture.recorder.waitFor(logged("chose 3")).has_value());
    status("wheel", true, -1);
    status("wheel", false, -1);
    status("pointer", true, -1);
    REQUIRE(fixture.recorder.waitFor(logged("show")).has_value());
    status("pointer", false, -1);
    REQUIRE(fixture.recorder.waitFor(logged("hide")).has_value());
    CHECK(fixture.recorder.count(logged("chose 3")) == 1);
}

TEST_CASE("ADR-0094: an action wheel tells its cells and names, runs a cell and names the chosen cell")
{
    Fixture fixture;
    fixture.services->addControl(
        {"items", "Items", "actionWheel", true,
         R"({"inputs":{"press":"KeyB"},"cells":[{"name":"potion","action":"control","control":"potion"},{"action":"logic"},{"name":"map","action":"logic"}]})"});
    const ScriptId script = fixture.load(R"js(
controls.items.onChoose(({ cell, name }) => log.info("ran", cell, name));
export async function check() {
    log.info(controls.items instanceof ActionWheel, controls.items.cells, controls.items.names.join("|"),
             controls.items.open, controls.items.cell);
    let code = "none";
    try {
        controls.items.choose(3);
    } catch (error) {
        code = error.message.split(" ")[0];
    }
    log.info("code", code);
    await controls.items.choose(1);
    log.info("chosen");
}
)js");
    fixture.services->setStatus({.touching = false, .cell = -1});
    CHECK(fixture.run(script, "check").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("true 3 potion||map false -1")) == 1);
    CHECK(fixture.recorder.count(logged("code GS-API-017")) == 1);
    CHECK(fixture.recorder.count(logged("chosen")) == 1);
    REQUIRE(fixture.services->events().size() == 3);
    CHECK(fixture.services->events()[0] == "control items down");
    CHECK(fixture.services->events()[1].starts_with("act items turn "));
    CHECK(fixture.services->events()[2] == "control items up");

    fixture.host->event("status", "items",
                        R"({"touching":true,"stick":{"x":0,"y":0},"aiming":false,"walking":false,"cancelled":false,"cell":2})");
    fixture.host->event("status", "items",
                        R"({"touching":false,"stick":{"x":0,"y":0},"aiming":false,"walking":false,"cancelled":false,"cell":-1})");
    REQUIRE(fixture.recorder.waitFor(logged("ran 2 map")).has_value());
}

TEST_CASE("issue 31: doN counts up to its limit, sequence runs in order and multiGate gives the next part each time")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function run() {
    const passes = [];
    for (let n = 0; n < 5; n += 1) {
        passes.push(doN("dash", 3));
    }
    log.info(passes.join(","));
    resetDoN("dash");
    log.info(doN("dash", 3), doN("dash", 3));
    const order = [];
    await sequence(async () => { order.push("a"); }, async () => { order.push("b"); });
    log.info("sequence", order.join(""));
    const gates = [];
    for (let n = 0; n < 4; n += 1) {
        await multiGate("door", 2, false, false, async () => { gates.push("1"); }, async () => { gates.push("2"); },
                        async () => { gates.push("done"); });
    }
    log.info("gates", gates.join(","));
    const round = [];
    for (let n = 0; n < 4; n += 1) {
        await multiGate("round", 2, true, false, async () => { round.push("1"); }, async () => { round.push("2"); });
    }
    log.info("loop", round.join(","));
    const mixed = new Set();
    for (let n = 0; n < 40; n += 1) {
        await multiGate("mixed", 2, false, true, async () => { mixed.add("1"); }, async () => { mixed.add("2"); },
                        async () => { mixed.add("done"); });
    }
    log.info("mixed", mixed.size);
    let refused = "none";
    try {
        doN("dash", 0);
    } catch (error) {
        refused = error.message.split(" ")[0];
    }
    log.info("refused", refused);
}
)js");
    CHECK(fixture.run(script, "run").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("true,true,true,false,false")) == 1);
    CHECK(fixture.recorder.count(logged("true true")) == 1);
    CHECK(fixture.recorder.count(logged("sequence ab")) == 1);
    CHECK(fixture.recorder.count(logged("gates 1,2,done,done")) == 1);
    CHECK(fixture.recorder.count(logged("loop 1,2,1,2")) == 1);
    CHECK(fixture.recorder.count(logged("mixed 3")) == 1);
    CHECK(fixture.recorder.count(logged("refused GS-API-017")) == 1);
}

TEST_CASE("issue 31: the list, dictionary, set, text and math helpers change their values and answer")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function run() {
    const names = [];
    list.add(names, "bo");
    list.add(names, "cy");
    list.insert(names, 1, "al");
    log.info("list", names.join(","), list.size(names), list.indexOf(names, "cy"), list.has(names, "al"));
    list.setAt(names, 0, "di");
    log.info("set", names.join(","), list.at(names, 0), list.remove(names, "cy"), list.remove(names, "cy"));
    list.sort(names);
    log.info("sorted", names.join(","));
    log.info("removed", String(list.removeAt(names, 0)), names.join(","));
    list.clear(names);
    log.info("cleared", list.size(names));
    const counts = dict.make();
    dict.set(counts, "a", 1);
    dict.set(counts, "b", 2);
    log.info("dict", dict.get(counts, "a"), dict.has(counts, "c"), dict.size(counts), dict.keys(counts).join(""),
             dict.values(counts).join(""));
    log.info("dict gone", dict.remove(counts, "a"), dict.size(counts));
    dict.clear(counts);
    log.info("dict cleared", dict.size(counts));
    const seen = set.make();
    set.add(seen, "x");
    set.add(seen, "x");
    set.add(seen, "y");
    log.info("set", set.size(seen), set.has(seen, "x"), set.values(seen).join(""), set.remove(seen, "x"),
             set.size(seen));
    set.clear(seen);
    log.info("set cleared", set.size(seen));
    log.info("text", text.join(["a", "b"], "-"), text.format("Round {0} of {1}", 2, 3), text.find("hello", "ll"),
             text.has("hello", "he"), text.split("a,b", ",").join("|"), text.replace("aXa", "X", "Y"),
             text.upper("ab"), text.lower("AB"), text.trim("  a  "), text.length("abc"));
    log.info("math", math.round(1.6), math.floor(1.6), math.ceil(1.2), math.abs(-3), math.min(2, 5), math.max(2, 5));
    let refused = "none";
    try {
        list.add("nope", 1);
    } catch (error) {
        refused = error.message.split(" ")[0];
    }
    log.info("refused", refused);
    let position = "none";
    try {
        list.removeAt(["a"], 3);
    } catch (error) {
        position = error.message.split(" ")[0];
    }
    log.info("position", position);
    let untyped = "none";
    try {
        text.upper(3);
    } catch (error) {
        untyped = error.message.split(" ")[0];
    }
    log.info("untyped", untyped);
}
)js");
    CHECK(fixture.run(script, "run").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("list bo,al,cy 3 2 true")) == 1);
    CHECK(fixture.recorder.count(logged("set di,al,cy di true false")) == 1);
    CHECK(fixture.recorder.count(logged("sorted al,di")) == 1);
    CHECK(fixture.recorder.count(logged("removed al di")) == 1);
    CHECK(fixture.recorder.count(logged("cleared 0")) == 1);
    CHECK(fixture.recorder.count(logged("dict 1 false 2 ab 12")) == 1);
    CHECK(fixture.recorder.count(logged("dict gone true 1")) == 1);
    CHECK(fixture.recorder.count(logged("dict cleared 0")) == 1);
    CHECK(fixture.recorder.count(logged("set 2 true xy true 1")) == 1);
    CHECK(fixture.recorder.count(logged("set cleared 0")) == 1);
    CHECK(fixture.recorder.count(logged("text a-b Round 2 of 3 2 true a|b aYa AB ab a 3")) == 1);
    CHECK(fixture.recorder.count(logged("math 2 1 2 3 2 5")) == 1);
    CHECK(fixture.recorder.count(logged("refused GS-API-011")) == 1);
    CHECK(fixture.recorder.count(logged("position GS-API-017")) == 1);
    CHECK(fixture.recorder.count(logged("untyped GS-API-011")) == 1);
}
