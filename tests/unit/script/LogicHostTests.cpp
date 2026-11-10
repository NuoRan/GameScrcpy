#include "ScriptFixture.h"

#include "gs/script/LogicHost.h"
#include "gs/script/Permissions.h"
#include "gs/script/Resources.h"
#include "gs/script/Services.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::LogicHost;
using gs::script::Point;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptFiles;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::ScriptSource;
using gs::script::ServiceAnswer;
using gs::script::StateInfo;
using gs::script::TaskId;
using gs::script::VarValue;
using gs::script::test::kindIs;
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

class ChannelServices final : public gs::script::ScriptServices
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

    void key(std::string_view name, bool down) override { record(std::format("key {} {}", name, down ? "down" : "up")); }
    std::vector<ControlInfo> controls() override { return {}; }
    bool pressControl(std::string_view, bool) override { return false; }
    bool enableControl(std::string_view, bool) override { return false; }
    std::vector<StateInfo> states() override { return {}; }
    std::optional<VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view) override {}

    bool offersService(std::string_view name) override { return name.starts_with("mcp."); }

    ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        record(std::format("service {} {}", name, json));
        if (json.find("\"refused\"") != std::string_view::npos) {
            return {"null", "GS-API-074", "only the app script of an extension offers tools"};
        }
        return {"true", {}, {}};
    }

    void requestService(std::uint64_t id, std::string_view name, std::string_view json) override
    {
        record(std::format("request {} {} {}", id, name, json));
    }

    void dropService(std::uint64_t id) override { record(std::format("drop {}", id)); }

    [[nodiscard]] std::vector<std::string> events() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_events;
    }

    [[nodiscard]] bool waitFor(std::string_view text, std::chrono::milliseconds timeout = std::chrono::seconds(30))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return std::ranges::find(m_events, text) != m_events.end(); });
    }

private:
    void record(std::string event)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_events.push_back(std::move(event));
        }
        m_changed.notify_all();
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<std::string> m_events;
};

std::filesystem::path nowhere()
{
    return std::filesystem::temp_directory_path() /
           ("gs_nowhere_" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}()));
}

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

}

TEST_CASE("one table names the permission and whether a phone is needed for every service, app request and event")
{
    using namespace gs::script;
    CHECK(serviceRule("tap").permission == "device.input");
    CHECK(serviceRule("tap").phone);
    CHECK(serviceRule("type").permission == "device.input");
    CHECK(serviceRule("find").permission == "device.screen");
    CHECK(serviceRule("readText").permission == "device.screen");
    CHECK(serviceRule("readClipboard").permission == "device.clipboard");
    CHECK(serviceRule("controls").permission == "mapping");
    CHECK(serviceRule("setGlobal").permission == "mapping");
    CHECK(serviceRule("keyDown").permission == "input.read");
    CHECK(serviceRule("cursor").permission == "input.read");
    CHECK(serviceRule("saveCapture").permission == kNeverGranted);
    CHECK(serviceRule("device").permission.empty());
    CHECK(serviceRule("device").phone);
    CHECK(serviceRule("loadStore").permission.empty());
    CHECK_FALSE(serviceRule("loadStore").phone);
    CHECK(serviceRule("callService").permission.empty());
    CHECK_FALSE(serviceRule("requestService").phone);
    CHECK(serviceRule("somethingNew").permission == kNeverGranted);
    CHECK(appRule("launch").permission == "device.apps");
    CHECK(appRule("keymap").permission == "mapping");
    CHECK(appRule("notify").permission == "notify");
    CHECK_FALSE(appRule("notify").phone);
    CHECK(appRule("taskParams").permission.empty());
    CHECK_FALSE(appRule("taskParams").phone);
    CHECK(appRule("unknown").permission == "device.input");
    CHECK(eventRule("control", "jump").permission == "mapping");
    CHECK(eventRule("global", "score").permission == "mapping");
    CHECK(eventRule("key", "KeyA").permission == "input.read");
    CHECK(eventRule("app", "foreground").permission == "device.apps");
    CHECK(eventRule("app", "inputBox").permission == "device.input");
    CHECK(eventRule("app", "rotation").permission.empty());
    CHECK(eventRule("mcp", "7").permission.empty());
    CHECK(permits({}, ""));
    CHECK_FALSE(permits({"device.input"}, kNeverGranted));
    CHECK(permits({"device.input"}, "device.input"));
    CHECK_FALSE(permits({"device.input"}, "mapping"));
    CHECK(permissionProblem("mapping").starts_with("GS-API-072"));
    CHECK(permissionProblem("mapping").find("\"mapping\"") != std::string::npos);
    CHECK(permissionProblem(kNeverGranted).starts_with("GS-API-072"));
    CHECK(phoneProblem().starts_with("GS-API-073"));
}

TEST_CASE("logic loads from the files it is given, TypeScript and resources too, and never from its folder")
{
    Recorder recorder;
    ScriptHost host(recorder.sink(), gs::script::Limits{}, std::make_shared<ChannelServices>());
    ScriptFolder disk;
    disk.write("main.ts", "log.info(\"from disk\");\nexport function hello() {}\n");
    disk.write("lib/disk.ts", "export const where = \"disk\";\n");
    auto files = std::make_shared<ScriptFiles>();
    files->emplace("main.ts", R"ts(import { twice } from "./lib/util.ts";
const button = res.point("Button");
log.info("from memory", twice(21), button.x, button.y);
export function hello(): void {
    log.info("hello from memory");
}
)ts");
    files->emplace("lib/util.ts", "export function twice(value: number): number {\n    return value * 2;\n}\n");
    files->emplace("broken.ts", "import { where } from \"./lib/disk.ts\";\nlog.info(where);\n");
    gs::script::Resources resources;
    resources.points.push_back({"Button", {0.25, 0.75}});
    files->emplace(std::string(gs::script::kResourcesFile), gs::script::writeResources(resources));

    ScriptId script = 0;
    const auto loaded = loadScript(host, recorder, ScriptSource{.folder = disk.path(), .entry = "main.ts", .files = files},
                                   &script);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    CHECK(loaded->kind == ReportKind::ScriptLoaded);
    CHECK(recorder.count(logged("from memory 42 0.25 0.75")) == 1);
    CHECK(recorder.count(logged("from disk")) == 0);
    const TaskId task = host.start(script, "hello");
    const auto ended = recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(recorder.count(logged("hello from memory")) == 1);

    const auto broken =
        loadScript(host, recorder, ScriptSource{.folder = disk.path(), .entry = "broken.ts", .files = files});
    REQUIRE(broken.has_value());
    CHECK(broken->kind == ReportKind::ScriptFailed);
    CHECK(broken->message.find("disk.ts") != std::string::npos);

    const auto lost = loadScript(host, recorder, ScriptSource{.folder = nowhere(), .entry = "gone.ts", .files = files});
    REQUIRE(lost.has_value());
    CHECK(lost->kind == ReportKind::ScriptFailed);
    CHECK(lost->message.find("gone.ts") != std::string::npos);
}

TEST_CASE("an extension's logic gets GS-API-072 for what it was not allowed and GS-API-073 without a phone, and "
          "nothing reaches the phone")
{
    Recorder recorder;
    const auto services = std::make_shared<ChannelServices>();
    ScriptHost host(recorder.sink(), gs::script::Limits{}, services);
    ScriptFolder folder;
    folder.write("main.js", std::string(kCodeOf) + R"js(
log.info("checked", codeOf(() => touch.tap(0.5, 0.5)), codeOf(() => device.info()), codeOf(() => store.set("k", 1)));
try {
    touch.tap(0.5, 0.5);
} catch (error) {
    log.info(error.message);
}
)js");

    ScriptSource denied = folder.source();
    denied.permissions = std::vector<std::string>{};
    const auto first = loadScript(host, recorder, denied);
    REQUIRE(first.has_value());
    CHECK(first->kind == ReportKind::ScriptLoaded);
    CHECK(recorder.count(logged("checked GS-API-072 none none")) == 1);
    CHECK(recorder.count(logged(gs::script::permissionProblem("device.input"))) == 1);
    CHECK(services->events().empty());

    ScriptSource allowed = folder.source();
    allowed.permissions = std::vector<std::string>{"device.input"};
    const std::size_t before = recorder.size();
    const auto second = loadScript(host, recorder, allowed);
    REQUIRE(second.has_value());
    CHECK(second->kind == ReportKind::ScriptLoaded);
    CHECK(recorder.count(logged("checked none none none"), before) == 1);
    CHECK(services->waitFor("tap 0.5 0.5 60"));

    ScriptSource phoneless = folder.source();
    phoneless.permissions = std::vector<std::string>{"device.input"};
    phoneless.phone = false;
    const std::size_t third = recorder.size();
    const auto lonely = loadScript(host, recorder, phoneless);
    REQUIRE(lonely.has_value());
    CHECK(lonely->kind == ReportKind::ScriptLoaded);
    CHECK(recorder.count(logged("checked GS-API-073 GS-API-073 none"), third) == 1);
    CHECK(recorder.count(logged(gs::script::phoneProblem()), third) == 1);
}

TEST_CASE("mcp.tool registers through host.call, runs each call as a task that answers with mcp.result, can be "
          "cancelled and leaves when disposed")
{
    Recorder recorder;
    const auto services = std::make_shared<ChannelServices>();
    ScriptHost host(recorder.sink(), gs::script::Limits{}, services);
    ScriptFolder folder;
    folder.write("main.js", std::string(kCodeOf) + R"js(
const greeting = mcp.tool("greet", { description: "Says hello",
    input: { type: "object", properties: { name: { type: "string" } } } }, async (input) => "Hello " + input.name);
mcp.tool("wait", { description: "Waits a long time" }, async () => {
    await sleep(60000);
    return "late";
});
mcp.tool("fail", { description: "Always fails" }, async () => {
    throw new Error("broken on purpose");
});
log.info("refused", codeOf(() => mcp.tool("refused", { description: "Not here" }, () => 1)),
         codeOf(() => mcp.tool("Bad", { description: "Bad name" }, () => 1)),
         codeOf(() => mcp.tool("nodesc", {}, () => 1)));
export function stop() {
    greeting.dispose();
    log.info("disposed");
}
)js");
    ScriptSource source = folder.source();
    source.permissions = std::vector<std::string>{};
    source.phone = false;
    ScriptId script = 0;
    const auto loaded = loadScript(host, recorder, source, &script);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    REQUIRE(loaded->kind == ReportKind::ScriptLoaded);
    CHECK(services->waitFor(
        R"(service mcp.register {"name":"greet","description":"Says hello","input":{"type":"object","properties":{"name":{"type":"string"}}}})"));
    CHECK(services->waitFor(R"(service mcp.register {"name":"wait","description":"Waits a long time","input":{"type":"object"}})"));
    CHECK(recorder.count(logged("refused GS-API-074 GS-API-074 GS-API-074")) == 1);

    host.event("mcp", "7", R"({"tool":"greet","arguments":{"name":"Ada"}})");
    CHECK(services->waitFor(R"(service mcp.result {"call":"7","ok":true,"value":"Hello Ada"})"));
    host.event("mcp", "9", R"({"tool":"fail","arguments":{}})");
    CHECK(services->waitFor(R"(service mcp.result {"call":"9","ok":false,"error":"broken on purpose"})"));
    host.event("mcp", "10", R"({"tool":"nope","arguments":{}})");
    CHECK(services->waitFor(
        R"(service mcp.result {"call":"10","ok":false,"error":"GS-API-074 the tool nope is not offered any more"})"));

    const std::size_t before = recorder.size();
    host.event("mcp", "8", R"({"tool":"wait","arguments":{}})");
    host.event("mcp", "8", R"({"cancel":true})");
    CHECK(recorder.waitFor(kindIs(ReportKind::TaskCancelled), before).has_value());
    host.event("mcp", "11", R"({"tool":"greet","arguments":{"name":"Bo"}})");
    CHECK(services->waitFor(R"(service mcp.result {"call":"11","ok":true,"value":"Hello Bo"})"));
    for (const std::string& event : services->events()) {
        CHECK(event.find("\"call\":\"8\"") == std::string::npos);
    }

    const TaskId task = host.start(script, "stop");
    const auto ended = recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(services->waitFor(R"(service mcp.unregister {"name":"greet"})"));
}

TEST_CASE("logic runs through the LogicHost interface that the remote host shares")
{
    Recorder recorder;
    const auto services = std::make_shared<ChannelServices>();
    std::unique_ptr<LogicHost> host = std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{}, services);
    CHECK(host->started());
    CHECK(host->problem().empty());
    ScriptFolder folder;
    folder.write("main.js", R"js(
export async function play() {
    await touch.swipe(0.1, 0.2, 0.3, 0.4, 40);
    log.info("swiped");
}
)js");
    const ScriptId script = host->load(folder.source());
    const auto loaded = recorder.waitFor(gs::script::test::loadedOrFailed(script));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->report.kind == ReportKind::ScriptLoaded);
    host->setFrameRate(60.0);
    const TaskId task = host->start(script, "play");
    const auto ended = recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(recorder.count(logged("swiped")) == 1);
    CHECK(services->events() == std::vector<std::string>{"swipe 0.1 0.2 0.3 0.4 40"});
    host->unload(script);
}
