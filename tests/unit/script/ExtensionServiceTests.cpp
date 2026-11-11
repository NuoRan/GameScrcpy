#include "ScriptFixture.h"

#include "gs/script/ApiFiles.h"
#include "gs/script/Services.h"

#include <doctest/doctest.h>

#include <algorithm>
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
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

class ServiceFake final : public gs::script::ScriptServices
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
        NodeInfo flash{"alice.tools", "Tools", "flash", {}, NodeType::Json, "", {"ai"}};
        flash.job = true;
        return {{"alice.tools", "Tools", "secret", {}, NodeType::String, "", {"ai"}}, std::move(flash)};
    }

    NodeAnswer callNode(std::string_view, std::string_view node, const std::vector<NodeValue>&) override
    {
        record("node " + std::string(node));
        NodeValue value;
        value.type = NodeType::String;
        value.text = "hidden";
        return {value, {}, {}};
    }

    bool offersService(std::string_view name) override { return name.starts_with("ext.") || name.starts_with("native."); }

    gs::script::ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        record(std::string(name) + " " + std::string(json));
        if (name == "ext.provide" && json.find("\"unknown\"") != std::string_view::npos) {
            return {"null", "GS-API-145", "unknown is not a service of alice.tools"};
        }
        return {name == "native.jobStart" ? R"({"id":"5"})" : "null", {}, {}};
    }

    void requestService(std::uint64_t request, std::string_view name, std::string_view json) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_requests.push_back(request);
        }
        record(std::string(name) + " " + std::string(json));
    }

    void dropService(std::uint64_t request) override { record("drop " + std::to_string(request)); }

    std::optional<std::uint64_t> waitForRequest(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_requests.size() > index; })) {
            return std::nullopt;
        }
        return m_requests[index];
    }

    bool waitForSeen(std::size_t count)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_seen.size() >= count; });
    }

    bool waitForLine(std::string_view prefix)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::seconds(30), [&] {
            return std::ranges::any_of(m_seen, [prefix](const std::string& each) { return each.starts_with(prefix); });
        });
    }

    std::vector<std::string> seen()
    {
        const std::scoped_lock lock(m_mutex);
        return m_seen;
    }

    std::vector<std::string> seenStarting(std::string_view prefix)
    {
        std::vector<std::string> found;
        for (const std::string& each : seen()) {
            if (each.starts_with(prefix)) {
                found.push_back(each);
            }
        }
        return found;
    }

private:
    void record(std::string line)
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_seen.push_back(std::move(line));
        }
        m_changed.notify_all();
    }

    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<std::string> m_seen;
    std::vector<std::uint64_t> m_requests;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<ServiceFake> services = std::make_shared<ServiceFake>();
    std::unique_ptr<ScriptHost> host = std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{}, services);

    ScriptId load(std::string_view main, std::optional<std::vector<std::string>> permissions = std::nullopt)
    {
        folder.write("main.js", main);
        gs::script::ScriptSource source = folder.source();
        source.permissions = std::move(permissions);
        source.phone = !source.permissions.has_value();
        ScriptId id = 0;
        const auto report = loadScript(*host, recorder, source, &id);
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

TEST_CASE("ADR-0112: ext.call sends the service and its arguments with the task's limit, gives the result, and refuses "
          "names and values it cannot send")
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
export async function sum() {
    log.info(typeof ext.call, ext.call.name, typeof ext.provide, ext.provide.name, Object.keys(ext).length);
    const result = await ext.call("alice.tools/add", { a: 1, b: 2 });
    log.info("sum", result);
    try {
        await ext.call("alice.tools/add", { a: 1, b: 2 });
    } catch (error) {
        log.info("refused", error.message);
    }
}
export function wrong() {
    log.info(["add", "/add", "alice/add", "alice.tools/Add", "alice.tools/", 7, "a".repeat(65) + ".b/add"]
                 .map((service) => codeOf(() => ext.call(service, {}))).join(" "),
             codeOf(() => ext.call("alice.tools/add", () => 1)), codeOf(() => ext.call("alice.tools/add", 1n)));
}
export async function waiting() {
    await ext.call("alice.tools/add", null);
}
)js");
    const TaskId task = fixture.host->start(script, "sum");
    const std::optional<std::uint64_t> first = fixture.services->waitForRequest(0);
    REQUIRE(first.has_value());
    fixture.host->answer(*first, "3");
    const std::optional<std::uint64_t> second = fixture.services->waitForRequest(1);
    REQUIRE(second.has_value());
    fixture.host->refuse(*second, "GS-API-147 bob.app may call alice.tools/add only when it lists alice.tools");
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    INFO(gs::script::describe(ended->report));
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("function call function provide 1")) == 1);
    CHECK(fixture.recorder.count(logged("sum 3")) == 1);
    CHECK(fixture.recorder.count(
              logged("refused GS-API-147 bob.app may call alice.tools/add only when it lists alice.tools")) == 1);
    CHECK(fixture.services->seenStarting("ext.call ") ==
          std::vector<std::string>{R"(ext.call {"service":"alice.tools/add","args":"{\"a\":1,\"b\":2}","limit":null})",
                                   R"(ext.call {"service":"alice.tools/add","args":"{\"a\":1,\"b\":2}","limit":null})"});

    CHECK(fixture.run(script, "wrong").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-146 GS-API-146 GS-API-146 GS-API-146 GS-API-146 GS-API-146 GS-API-146 "
                                        "GS-API-148 GS-API-148")) == 1);

    const TaskId waiting = fixture.host->start(script, "waiting");
    REQUIRE(fixture.services->waitForRequest(2).has_value());
    fixture.host->cancel(waiting);
    REQUIRE(fixture.recorder.waitFor(taskEnded(waiting)).has_value());
    REQUIRE(fixture.services->waitForSeen(4));
    CHECK(fixture.services->seenStarting("drop ").size() == 1);
    CHECK(fixture.services->seenStarting("ext.call ").back() ==
          R"(ext.call {"service":"alice.tools/add","args":"null","limit":null})");
}

TEST_CASE("ADR-0112: ext.provide answers each call of a declared service in a task of its own with the caller and the "
          "permissions, and reports what the handler throws or returns that is not JSON")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
ext.provide("add", async ({ a, b }, { caller, permissions, signal }) => {
    log.info("serving", caller, permissions.join(","), signal.aborted, Object.isFrozen(permissions));
    return a + b;
});
ext.provide("broken", () => {
    throw new Error("no luck");
});
ext.provide("weird", () => () => 1);
ext.provide("nothing", async () => {});
const gone = ext.provide("gone", () => 1);
gone.dispose();
export function wrong() {
    const codes = [];
    for (const name of ["Add", "", "a-b", 7]) {
        try {
            ext.provide(name, () => 1);
            codes.push("none");
        } catch (error) {
            codes.push(error.message.split(" ")[0]);
        }
    }
    try {
        ext.provide("add", 5);
    } catch (error) {
        codes.push(error.message.split(" ")[0]);
    }
    try {
        ext.provide("unknown", () => 1);
    } catch (error) {
        codes.push(error.message);
    }
    log.info(codes.join(" | "));
}
)js",
                                         std::vector<std::string>{"notify"});
    fixture.host->event("extService", "7",
                        R"({"type":"call","name":"add","caller":"bob.app","permissions":["notify"],"args":{"a":2,"b":3}})");
    fixture.host->event("extService", "8", R"({"type":"call","name":"broken","caller":"","permissions":[],"args":null})");
    fixture.host->event("extService", "9", R"({"type":"call","name":"weird","caller":"","permissions":[],"args":null})");
    fixture.host->event("extService", "10", R"({"type":"call","name":"gone","caller":"","permissions":[],"args":null})");
    fixture.host->event("extService", "11", R"({"type":"call","name":"nothing","caller":"","permissions":[],"args":1})");
    REQUIRE(fixture.services->waitForSeen(11));
    std::vector<std::string> settled = fixture.services->seenStarting("ext.answer ");
    for (const std::string& each : fixture.services->seenStarting("ext.fail ")) {
        settled.push_back(each);
    }
    std::ranges::sort(settled);
    CHECK(settled ==
          std::vector<std::string>{
              R"(ext.answer {"call":"11","result":"null"})", R"(ext.answer {"call":"7","result":"5"})",
              R"(ext.fail {"call":"10","message":"GS-API-146 gone is not provided now","output":false})",
              R"(ext.fail {"call":"8","message":"no luck","output":false})",
              R"x(ext.fail {"call":"9","message":"GS-API-148 the result must be JSON values (no functions or undefined)","output":true})x"});
    CHECK(fixture.recorder.count(logged("serving bob.app notify false true")) == 1);
    CHECK(fixture.services->seenStarting("ext.provide ").size() == 5);
    CHECK(fixture.services->seenStarting("ext.withdraw ") == std::vector<std::string>{R"(ext.withdraw {"name":"gone"})"});

    CHECK(fixture.run(script, "wrong").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-145 | GS-API-145 | GS-API-145 | GS-API-145 | GS-API-008 | "
                                        "GS-API-145 unknown is not a service of alice.tools")) == 1);
}

TEST_CASE("ADR-0112: a service call runs with the permissions of both extensions, passes them on to its own calls and "
          "child tasks, and stops when the caller cancels")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = (action) => {
    try {
        action();
        return "allowed";
    } catch (error) {
        return error.message.split(" ")[0];
    }
};
ext.provide("note", async (args) => {
    const own = codeOf(() => ui.notify("a", "b"));
    const node = codeOf(() => ext.alice.tools.secret());
    const job = args.job ? codeOf(() => ext.alice.tools.flash({})) : "skipped";
    let child = "";
    await new Promise((resolve) => after(1, () => {
        child = codeOf(() => ui.notify("c", "d"));
        resolve();
    }));
    return [own, node, job, child].join(" ");
});
ext.provide("forward", async () => ext.call("carol.more/x", {}));
ext.provide("hold", async ({}, { signal }) => {
    await sleep(60000);
    return signal.aborted;
});
export function outside() {
    log.info(codeOf(() => ui.notify("e", "f")), codeOf(() => ext.alice.tools.secret()));
}
)js",
                                         std::vector<std::string>{"notify", "ai"});
    fixture.host->event("extService", "1",
                        R"({"type":"call","name":"note","caller":"bob.app","permissions":["notify"],"args":{"job":true}})");
    fixture.host->event("extService", "2",
                        R"({"type":"call","name":"note","caller":"carol.x","permissions":[],"args":{"job":true}})");
    fixture.host->event("extService", "3",
                        R"({"type":"call","name":"note","caller":"","permissions":["ai","notify"],"args":{}})");
    REQUIRE(fixture.services->waitForSeen(7));
    std::vector<std::string> answers = fixture.services->seenStarting("ext.answer ");
    std::ranges::sort(answers);
    CHECK(answers == std::vector<std::string>{
                         R"(ext.answer {"call":"1","result":"\"allowed GS-API-072 GS-API-072 allowed\""})",
                         R"(ext.answer {"call":"2","result":"\"GS-API-072 GS-API-072 GS-API-072 GS-API-072\""})",
                         R"(ext.answer {"call":"3","result":"\"allowed allowed skipped allowed\""})"});
    CHECK(fixture.services->seenStarting("node ") == std::vector<std::string>{"node secret"});
    CHECK(fixture.services->seenStarting("native.jobStart ").empty());

    fixture.host->event("extService", "4",
                        R"({"type":"call","name":"forward","caller":"bob.app","permissions":["notify"],"args":{}})");
    const std::optional<std::uint64_t> forwarded = fixture.services->waitForRequest(0);
    REQUIRE(forwarded.has_value());
    CHECK(fixture.services->seenStarting("ext.call ") ==
          std::vector<std::string>{R"(ext.call {"service":"carol.more/x","args":"{}","limit":["notify"]})"});
    const std::size_t before = fixture.services->seen().size();
    fixture.host->answer(*forwarded, "\"done\"");
    REQUIRE(fixture.services->waitForSeen(before + 1));
    CHECK(fixture.services->seenStarting("ext.answer ").back() == R"(ext.answer {"call":"4","result":"\"done\""})");

    fixture.host->event("extService", "5", R"({"type":"call","name":"hold","caller":"bob.app","permissions":[],"args":{}})");
    fixture.host->event("extService", "5", R"({"type":"cancel"})");
    REQUIRE(fixture.services->waitForLine(R"(ext.fail {"call":"5")"));
    const std::vector<std::string> stopped = fixture.services->seenStarting(R"(ext.fail {"call":"5")");
    REQUIRE(stopped.size() == 1);
    CHECK(stopped[0].find("cancelled") != std::string::npos);
    CHECK(fixture.services->seenStarting("ext.answer ").size() == 4);

    CHECK(fixture.run(script, "outside").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("allowed allowed")) == 1);
}

TEST_CASE("ADR-0112: events.on hears \"<extension id>:<topic>\" from an extension this one depends on and refuses "
          "a topic with a malformed id")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const heard = [];
events.on("alice.boards:board.added", ({ topic, value }) => {
    heard.push(topic + " " + JSON.stringify(value));
});
export function report() {
    log.info("heard", heard.join(","));
}
export function wrong() {
    log.info(["Alice.boards:x", "alice:x", "alice.boards:", ":x", "alice.boards:Board", "a.b:c:d"].map((topic) => {
        try {
            events.on(topic, () => {});
            return "none";
        } catch (error) {
            return error.message.split(" ")[0];
        }
    }).join(" "));
}
)js");
    fixture.host->event("extEvent", "alice.boards:board.added", R"({"id":"b1"})");
    fixture.host->event("extEvent", "board.added", R"({"id":"b2"})");
    CHECK(fixture.run(script, "report").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("heard alice.boards:board.added {\"id\":\"b1\"}")) == 1);
    CHECK(fixture.run(script, "wrong").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-068 GS-API-068 GS-API-068 GS-API-068 GS-API-068 GS-API-068")) == 1);
}

TEST_CASE("ADR-0112: ext.call and ext.provide are typed members of Extensions, and an extension named call or provide "
          "is left out of types/extensions.d.ts")
{
    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const auto declarations = std::ranges::find(files, std::string("types/gamescrcpy.d.ts"), &gs::script::ApiFile::path);
    REQUIRE(declarations != files.end());
    CHECK(declarations->contents.find("    call<T = StoreValue>(service: string, args?: StoreValue): Promise<T>;") !=
          std::string::npos);
    CHECK(declarations->contents.find("    provide<A = any>(name: string, handler: (args: A, context: ServiceContext) "
                                      "=> StoreValue | void | Promise<StoreValue | void>): Subscription;") !=
          std::string::npos);
    CHECK(declarations->contents.find("type ServiceContext = TaskContext & { readonly caller: string; readonly "
                                      "permissions: readonly string[] };") != std::string::npos);
    CHECK(gs::script::extensionTypes({{"call.me", "", "now", {}, NodeType::String, "", {}},
                                      {"provide.it", "", "now", {}, NodeType::String, "", {}}}) ==
          "interface Extensions {}\n");
}
