#include "ScriptFixture.h"

#include "gs/script/ApiFiles.h"
#include "gs/script/Services.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::NodeAnswer;
using gs::script::NodeInfo;
using gs::script::NodeType;
using gs::script::NodeValue;
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

std::vector<NodeInfo> sampleNodes()
{
    return {
        {"gamescrcpy.pc-clock", "PC clock", "now", {}, NodeType::String, "The time.", {}},
        {"alice.net-tools",
         "Net tools",
         "fetch",
         {{"url", NodeType::String}, {"wait", NodeType::Duration}},
         NodeType::Json,
         "",
         {"network"}},
        {"alice.net-tools",
         "Net tools",
         "mark",
         {{"at", NodeType::Point},
          {"area", NodeType::Region},
          {"tint", NodeType::Color},
          {"on", NodeType::Boolean},
          {"count", NodeType::Number}},
         NodeType::Point,
         "",
         {}},
    };
}

class NodeServices final : public gs::script::ScriptServices
{
public:
    DeviceInfo device() override { return {"Pixel", 15, 1080, 2400, 0}; }
    void tap(std::uint64_t, Point, double) override {}
    void swipe(std::uint64_t, Point, Point, double) override {}
    void key(std::string_view, bool) override {}
    std::vector<ControlInfo> controls() override { return {}; }
    bool pressControl(std::string_view, bool) override { return false; }
    bool enableControl(std::string_view, bool) override { return false; }
    std::vector<StateInfo> states() override { return {}; }
    std::optional<gs::script::VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const gs::script::VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view) override {}

    std::vector<NodeInfo> nodes() override
    {
        if (m_hidden.load()) {
            return {};
        }
        std::vector<NodeInfo> all = sampleNodes();
        NodeInfo flash{"alice.net-tools", "Net tools", "flash", {{"port", NodeType::String}, {"size", NodeType::Number}},
                       NodeType::Json, "Writes firmware.", {}};
        flash.job = true;
        all.push_back(std::move(flash));
        return all;
    }

    bool offersService(std::string_view name) override { return name.starts_with("native.job"); }

    gs::script::ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        const std::scoped_lock lock(m_mutex);
        m_services.push_back(std::string(name) + " " + std::string(json));
        return {name == "native.jobStart" ? R"({"id":"5"})" : "null", {}, {}};
    }

    void requestService(std::uint64_t request, std::string_view name, std::string_view json) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_services.push_back(std::string(name) + " " + std::string(json));
            m_requests.push_back(request);
        }
        m_requested.notify_all();
    }

    std::optional<std::uint64_t> waitForRequest(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_requested.wait_for(lock, std::chrono::seconds(30), [&] { return m_requests.size() > index; })) {
            return std::nullopt;
        }
        return m_requests[index];
    }

    std::vector<std::string> services()
    {
        const std::scoped_lock lock(m_mutex);
        return m_services;
    }

    NodeAnswer callNode(std::string_view extension, std::string_view node, const std::vector<NodeValue>& values) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_calls.push_back(std::string(extension) + "." + std::string(node));
            m_values.insert(m_values.end(), values.begin(), values.end());
        }
        if (node == "now") {
            NodeValue time;
            time.type = NodeType::String;
            time.text = "12:34:56";
            return {time, {}, {}};
        }
        if (node == "mark") {
            NodeValue where;
            where.type = NodeType::Point;
            where.area = gs::script::Region{values[0].area.x, values[0].area.y, 0.0, 0.0};
            return {where, {}, {}};
        }
        return {{}, "GS-API-063", "it needs the permission \"network\", which Net tools has not been allowed"};
    }

    void hide() { m_hidden.store(true); }

    std::vector<std::string> calls()
    {
        const std::scoped_lock lock(m_mutex);
        return m_calls;
    }

    std::vector<NodeValue> values()
    {
        const std::scoped_lock lock(m_mutex);
        return m_values;
    }

private:
    std::atomic<bool> m_hidden{false};
    std::mutex m_mutex;
    std::condition_variable m_requested;
    std::vector<std::string> m_calls;
    std::vector<NodeValue> m_values;
    std::vector<std::string> m_services;
    std::vector<std::uint64_t> m_requests;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<NodeServices> services = std::make_shared<NodeServices>();
    std::unique_ptr<ScriptHost> host =
        std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{}, services);

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

TEST_CASE("ADR-0097: logic calls the system nodes of native extensions as ext.<extension>.<node>(...)")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export function run() {
    log.info(typeof ext, typeof ext.gamescrcpy.pcClock.now, ext.gamescrcpy.pcClock.now.length,
             ext.alice.netTools.fetch.length, ext.gamescrcpy.pcClock.now.name);
    log.info(ext.gamescrcpy.pcClock.now());
    const where = ext.alice.netTools.mark({ x: 0.25, y: 0.5 }, { x: 0, y: 0, width: 0.5, height: 0.5 }, "#ff8800",
                                          true, 3);
    log.info(where.x, where.y);
    log.info(Object.isFrozen(ext), Object.isFrozen(ext.alice), Object.isFrozen(ext.alice.netTools));
}
)js");
    const Report ended = fixture.run(script, "run");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("object function 0 2 now")) == 1);
    CHECK(fixture.recorder.count(logged("12:34:56")) == 1);
    CHECK(fixture.recorder.count(logged("0.25 0.5")) == 1);
    CHECK(fixture.recorder.count(logged("true true true")) == 1);
    CHECK(fixture.services->calls() == std::vector<std::string>{"gamescrcpy.pc-clock.now", "alice.net-tools.mark"});
    const std::vector<NodeValue> values = fixture.services->values();
    REQUIRE(values.size() == 5);
    CHECK(values[0].type == NodeType::Point);
    CHECK(values[0].area.x == 0.25);
    CHECK(values[1].type == NodeType::Region);
    CHECK(values[1].area.width == 0.5);
    CHECK(values[2].type == NodeType::Color);
    CHECK(values[2].text == "#FF8800");
    CHECK(values[3].flag);
    CHECK(values[4].number == 3.0);
}

TEST_CASE("ADR-0097: a node refuses values of the wrong type and names a permission the extension lacks")
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
const area = { x: 0, y: 0, width: 1, height: 1 };
export function wrong() {
    log.info(codeOf(() => ext.alice.netTools.mark(1, area, "#ff8800", true, 3)),
             codeOf(() => ext.alice.netTools.mark({ x: 0.1, y: 0.1 }, area, "#ff8800", "yes", 3)),
             codeOf(() => ext.alice.netTools.mark({ x: 0.1, y: 0.1 }, area, "#ff8800", true, Infinity)),
             codeOf(() => ext.alice.netTools.fetch("https://example.com")),
             codeOf(() => ext.alice.netTools.fetch(5, 10)),
             codeOf(() => ext.alice.netTools.fetch("https://example.com", -1)),
             codeOf(() => ext.alice.netTools.fetch("https://example.com", 10)));
    try {
        ext.alice.netTools.fetch("https://example.com", 10);
    } catch (error) {
        log.info(error.message.includes("ext.alice.netTools.fetch"), error.message.includes("\"network\""));
    }
}
)js");
    const Report ended = fixture.run(script, "wrong");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(
              logged("GS-API-002 GS-API-061 GS-API-061 GS-API-061 GS-API-061 GS-API-061 GS-API-063"))
          == 1);
    CHECK(fixture.recorder.count(logged("true true")) == 1);
    CHECK(fixture.services->calls()
          == std::vector<std::string>{"alice.net-tools.fetch", "alice.net-tools.fetch"});
}

TEST_CASE("ADR-0097: a node that is no longer offered says so, and the ext object follows the installed nodes")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export function stale() {
    try {
        ext.gamescrcpy.pcClock.now();
    } catch (error) {
        log.info(error.message.split(" ")[0]);
    }
}
export function fresh() {
    log.info(typeof ext.gamescrcpy, Object.keys(ext).length);
}
)js");
    fixture.services->hide();
    CHECK(fixture.run(script, "stale").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-060")) == 1);
    fixture.host->event("nodes", "", "{}");
    CHECK(fixture.run(script, "fresh").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("undefined 0")) == 1);
}

TEST_CASE("ADR-0112: a job of a native extension starts with its arguments by name, hears its progress and log "
          "lines, gives its result and can be cancelled")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function flash() {
    const job = ext.alice.netTools.flash({ port: "COM3", size: 2 });
    const heard = [];
    job.onProgress(() => {
        throw new Error("the listener broke");
    });
    const { onLog } = job;
    const chained = job.onProgress((fraction, text) => heard.push(fraction + " " + text))
                       .onLog((level, text) => heard.push(level + " " + text))
                       .onOutput((value) => heard.push("out " + JSON.stringify(value)));
    log.info(chained === job, onLog(() => {}) === job, typeof job.cancel, Object.isFrozen(job),
             ext.alice.netTools.flash.length);
    const value = await job.result;
    log.info(JSON.stringify(value), heard.join(","));
}
export async function stop() {
    const job = ext.alice.netTools.flash({ port: "COM4", size: 1 });
    job.cancel();
    try {
        await job.result;
    } catch (error) {
        log.info("stopped", error.message);
    }
}
)js");
    const TaskId task = fixture.host->start(script, "flash");
    const std::optional<std::uint64_t> request = fixture.services->waitForRequest(0);
    REQUIRE(request.has_value());
    fixture.host->event("job", "5", R"({"kind":"progress","fraction":0.5,"text":"half"})");
    fixture.host->event("job", "6", R"({"kind":"log","level":2,"text":"not ours"})");
    fixture.host->event("job", "5", R"({"kind":"log","level":1,"text":"hot"})");
    fixture.host->event("job", "5", R"({"kind":"output","value":{"board":"A"}})");
    fixture.host->event("job", "6", R"({"kind":"output","value":"not ours"})");
    fixture.host->answer(*request, R"({"written":2})");
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    INFO(gs::script::describe(ended->report));
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("true true function true 1")) == 1);
    CHECK(fixture.recorder.count(logged("{\"written\":2} 0.5 half,1 hot,out {\"board\":\"A\"}")) == 1);
    const auto broke = fixture.recorder.waitFor(kindIs(ReportKind::UnhandledRejection));
    REQUIRE(broke.has_value());
    CHECK(broke->report.message.find("the listener broke") != std::string::npos);

    const TaskId stopping = fixture.host->start(script, "stop");
    const std::optional<std::uint64_t> second = fixture.services->waitForRequest(1);
    REQUIRE(second.has_value());
    fixture.host->refuse(*second, "GS-API-066 cancelled");
    const auto stopped = fixture.recorder.waitFor(taskEnded(stopping));
    REQUIRE(stopped.has_value());
    CHECK(stopped->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("stopped GS-API-066 cancelled")) == 1);
    CHECK(fixture.services->services()
          == std::vector<std::string>{
              R"(native.jobStart {"extension":"alice.net-tools","job":"flash","args":{"port":"COM3","size":2}})",
              R"(native.jobResult {"id":"5"})",
              R"(native.jobStart {"extension":"alice.net-tools","job":"flash","args":{"port":"COM4","size":1}})",
              R"(native.jobResult {"id":"5"})",
              R"(native.jobCancel {"id":"5"})"});
    CHECK(fixture.services->calls().empty());

    NodeInfo bare{"a.b", "", "scan", {}, NodeType::None, "", {}};
    bare.job = true;
    CHECK(gs::script::nodeSignature(bare) == "scan(args?: {}): ExtensionJob<null>");
    NodeInfo typed{"a.b", "", "flash", {{"port", NodeType::String}, {"at", NodeType::Point}}, NodeType::Number, "", {}};
    typed.job = true;
    CHECK(gs::script::nodeSignature(typed) == "flash(args: { port: string; at: PicturePoint }): ExtensionJob<number>");
}

TEST_CASE("ADR-0112: follow hears a job's progress and runs done with its result or failed with its error, and "
          "without failed a failure throws")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function followed() {
    const heard = [];
    await ext.alice.netTools.flash({ port: "COM5", size: 3 }).follow(
        async (fraction, text) => { heard.push(fraction + " " + text); },
        async (result) => { heard.push("done " + JSON.stringify(result)); },
        async (error) => { heard.push("failed " + error.message); });
    log.info("followed", heard.join(","));
}
export async function failing() {
    const heard = [];
    await ext.alice.netTools.flash({ port: "COM6", size: 3 }).follow(undefined, () => heard.push("done"),
                                                                    (error) => heard.push("failed " + error.message));
    try {
        await ext.alice.netTools.flash({ port: "COM7", size: 3 }).follow();
    } catch (error) {
        heard.push("thrown " + error.message.split(" ")[0]);
    }
    log.info("failing", heard.join(","));
}
)js");
    const TaskId followed = fixture.host->start(script, "followed");
    const std::optional<std::uint64_t> first = fixture.services->waitForRequest(0);
    REQUIRE(first.has_value());
    fixture.host->event("job", "5", R"({"kind":"progress","fraction":0.25,"text":"quarter"})");
    fixture.host->answer(*first, R"({"ok":true})");
    const auto ended = fixture.recorder.waitFor(taskEnded(followed));
    REQUIRE(ended.has_value());
    INFO(gs::script::describe(ended->report));
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("followed 0.25 quarter,done {\"ok\":true}")) == 1);

    const TaskId failing = fixture.host->start(script, "failing");
    const std::optional<std::uint64_t> second = fixture.services->waitForRequest(1);
    REQUIRE(second.has_value());
    fixture.host->refuse(*second, "GS-API-065 asked to fail");
    const std::optional<std::uint64_t> third = fixture.services->waitForRequest(2);
    REQUIRE(third.has_value());
    fixture.host->refuse(*third, "GS-API-065 the port is gone");
    const auto stopped = fixture.recorder.waitFor(taskEnded(failing));
    REQUIRE(stopped.has_value());
    INFO(gs::script::describe(stopped->report));
    CHECK(stopped->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("failing failed GS-API-065 asked to fail,thrown GS-API-065")) == 1);
}

TEST_CASE("ADR-0112: events.on starts a task for each event of its topic with the topic and the value, and refuses a "
          "topic a DLL could not send")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const heard = [];
events.on("board.added", ({ topic, value }) => {
    heard.push(topic + " " + JSON.stringify(value));
});
export function report() {
    log.info("heard", heard.join(","));
}
export function wrong() {
    log.info(["Board", "", "a".repeat(65), 7].map((topic) => {
        try {
            events.on(topic, () => {});
            return "none";
        } catch (error) {
            return error.message.split(" ")[0];
        }
    }).join(" "));
}
)js");
    fixture.host->event("extEvent", "board.added", R"({"id":"b1"})");
    fixture.host->event("extEvent", "board.removed", R"({"id":"b1"})");
    fixture.host->event("extEvent", "board.added", "7");
    CHECK(fixture.run(script, "report").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("heard board.added {\"id\":\"b1\"},board.added 7")) == 1);
    CHECK(fixture.run(script, "wrong").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-068 GS-API-068 GS-API-068 GS-API-068")) == 1);
}

TEST_CASE("ADR-0112: events.on hears GameScrcpy's own events, such as a phone connecting, with their value in the "
          "same way as a DLL's")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const heard = [];
events.on("device.connected", ({ topic, value }) => {
    heard.push(topic + " " + value.phone + " " + value.name);
});
events.on("settings.changed", ({ value }) => {
    heard.push("settings " + value.keys.join("+") + " " + value.values["general.language"]);
});
export function report() {
    log.info("heard", heard.join(","));
}
)js");
    fixture.host->event("extEvent", "device.connected", R"({"phone":"R58M","name":"Pixel 8"})");
    fixture.host->event("extEvent", "device.disconnected", R"({"phone":"R58M","name":"Pixel 8"})");
    fixture.host->event("extEvent", "settings.changed",
                        R"({"keys":["general.language"],"values":{"general.language":"zh_CN"},"extension":null,)"
                        R"("device":null})");
    CHECK(fixture.run(script, "report").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("heard device.connected R58M Pixel 8,settings general.language zh_CN")) == 1);
}

TEST_CASE("ADR-0097: installed nodes are typed in types/extensions.d.ts under interface Extensions")
{
    CHECK(gs::script::nodePath("gamescrcpy.pc-clock", "now") == "ext.gamescrcpy.pcClock.now");
    CHECK(gs::script::nodePath("alice.1pass-word", "go") == "ext.alice[\"1passWord\"].go");
    CHECK(gs::script::nodeSegment("pc-2") == "pc_2");
    CHECK(gs::script::nodeOwner("a.b-c.d") == std::vector<std::string>{"a", "bC", "d"});
    CHECK(gs::script::nodeTypeNamed("") == NodeType::None);
    CHECK(gs::script::nodeTypeNamed("duration") == NodeType::Duration);
    CHECK_FALSE(gs::script::nodeTypeNamed("int").has_value());
    CHECK(gs::script::nodeSignature({"a.b", "", "go", {{"function", NodeType::String}, {"at", NodeType::Point}},
                                     NodeType::None, "", {}})
          == "go(function_: string, at: PicturePoint): void");

    CHECK(gs::script::extensionTypes({}) == "interface Extensions {}\n");
    CHECK(gs::script::extensionTypes(sampleNodes()) == R"ts(interface Extensions {
    readonly alice: {
        /** Net tools (alice.net-tools) */
        readonly netTools: {
            fetch(url: string, wait: number): StoreValue;
            mark(at: PicturePoint, area: Region, tint: HexColor, on: boolean, count: number): PicturePoint;
        };
    };
    readonly gamescrcpy: {
        /** PC clock (gamescrcpy.pc-clock) */
        readonly pcClock: {
            /** The time. */
            now(): string;
        };
    };
}
)ts");

    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const auto declarations = std::ranges::find(files, std::string("types/gamescrcpy.d.ts"), &gs::script::ApiFile::path);
    REQUIRE(declarations != files.end());
    CHECK(declarations->contents.find("declare const ext: Extensions;") != std::string::npos);
    CHECK(declarations->contents.find("interface Extensions {\n") != std::string::npos);
    CHECK(declarations->contents.find("export import ext = globalThis.ext;") != std::string::npos);
}
