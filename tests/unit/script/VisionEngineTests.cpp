#include "ScriptFixture.h"

#include "gs/script/Services.h"

#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::Point;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::ScriptSource;
using gs::script::ServiceAnswer;
using gs::script::StateInfo;
using gs::script::TaskId;
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

struct Asked {
    std::uint64_t id = 0;
    std::string service;
    std::string json;
};

class VisionServices final : public gs::script::ScriptServices
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

    bool offersService(std::string_view name) override { return name.starts_with("vision."); }

    ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        const std::scoped_lock lock(m_mutex);
        m_calls.push_back(std::string(name) + " " + std::string(json));
        if (name == "vision.engines") {
            return {R"([{"id":"system","title":"Windows","kinds":["ocr"],"languages":["en-US","zh-Hans-CN"]},)"
                    R"({"id":"alice.finder/boss","title":"Boss finder","kinds":["detect","classify"],"languages":[]}])",
                    {},
                    {}};
        }
        return {"null", {}, {}};
    }

    void requestService(std::uint64_t id, std::string_view name, std::string_view json) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_asked.push_back({id, std::string(name), std::string(json)});
        }
        m_changed.notify_all();
    }

    void dropService(std::uint64_t id) override
    {
        const std::scoped_lock lock(m_mutex);
        m_dropped.push_back(id);
    }

    std::optional<Asked> waitForAsk(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_asked.size() > index; })) {
            return std::nullopt;
        }
        return m_asked[index];
    }

    std::size_t asks() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_asked.size();
    }

    std::vector<std::string> calls() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_calls;
    }

    std::vector<std::uint64_t> dropped() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_dropped;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<Asked> m_asked;
    std::vector<std::string> m_calls;
    std::vector<std::uint64_t> m_dropped;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<VisionServices> services = std::make_shared<VisionServices>();
    std::unique_ptr<ScriptHost> host =
        std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{}, services);

    ScriptId load(std::string_view main, std::optional<std::vector<std::string>> permissions = std::nullopt)
    {
        folder.write("main.js", main);
        ScriptSource source = folder.source();
        source.permissions = std::move(permissions);
        ScriptId id = 0;
        const auto report = loadScript(*host, recorder, source, &id);
        REQUIRE(report.has_value());
        INFO(gs::script::describe(*report));
        REQUIRE(report->kind == ReportKind::ScriptLoaded);
        return id;
    }

    Asked asked(std::size_t index, std::string_view service)
    {
        const std::optional<Asked> found = services->waitForAsk(index);
        REQUIRE(found.has_value());
        CHECK(found->service == service);
        return *found;
    }

    Report finish(TaskId task)
    {
        const auto ended = recorder.waitFor(taskEnded(task));
        REQUIRE(ended.has_value());
        return ended->report;
    }
};

}

TEST_CASE("ADR-0112: vision.engines lists the recognition engines and what each recognizes, frozen, through the "
          "service channel")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function list() {
    const engines = vision.engines();
    log.info("engines", engines.length, engines[0].id, engines[0].title, engines[0].kinds.join(","),
             engines[0].languages.join(","), engines[1].id, engines[1].kinds.join(","), engines[1].languages.length);
    log.info("frozen", Object.isFrozen(engines), Object.isFrozen(engines[0]), Object.isFrozen(engines[0].kinds),
             Object.isFrozen(engines[1].languages));
}
)js");
    const TaskId task = fixture.host->start(script, "list");
    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(
              logged("engines 2 system Windows ocr en-US,zh-Hans-CN alice.finder/boss detect,classify 0")) == 1);
    CHECK(fixture.recorder.count(logged("frozen true true true true")) == 1);
    CHECK(fixture.services->calls() == std::vector<std::string>{"vision.engines {}"});
}

TEST_CASE("ADR-0112: vision.recognize and an engine's read, detect and classify ask with the engine, the kind, the "
          "region and the params, give frozen results or null, and check what they are given")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function look() {
    const lines = await vision.recognize("system", "ocr", { x: 0.25, y: 0.5, width: 0.5, height: 0.25 },
                                         { language: "en" });
    log.info("lines", lines.length, lines[0].text, lines[0].x, lines[0].confidence, Object.isFrozen(lines),
             Object.isFrozen(lines[0]));
    const engine = vision.engine("alice.finder/boss");
    log.info("engine", engine.id, Object.prototype.toString.call(engine), Object.isFrozen(engine));
    const found = await engine.detect();
    log.info("found", found.length, found[0].label, found[0].width, found[0].confidence);
    const none = await engine.classify(undefined, { threshold: 0.5 });
    log.info("none", none);
    const read = await vision.engine("system").read({ x: 0, y: 0, width: 1, height: 0.5 });
    log.info("read", read.length, Object.isFrozen(read));
    const tries = [
        ["kind", () => vision.recognize("system", "faces")],
        ["id", () => vision.recognize(7, "ocr")],
        ["region", () => vision.recognize("system", "ocr", { x: 0.75, y: 0, width: 0.5, height: 1 })],
        ["params", () => vision.recognize("system", "ocr", undefined, ["en"])],
        ["empty", () => vision.engine("")],
    ];
    for (const [name, attempt] of tries) {
        try {
            await attempt();
            log.info(name, "passed");
        } catch (error) {
            log.info(name, error.message.split(" ")[0], error.name);
        }
    }
    try {
        await vision.recognize("nobody/none", "detect");
    } catch (error) {
        log.info("refused", error.message);
    }
}
)js");
    const TaskId task = fixture.host->start(script, "look");
    const Asked lines = fixture.asked(0, "vision.recognize");
    CHECK(lines.json == R"({"engine":"system","kind":"ocr","region":{"x":0.25,"y":0.5,"width":0.5,"height":0.25},)"
                        R"("params":{"language":"en"}})");
    fixture.host->answer(lines.id,
                         R"([{"text":"Gold 120","x":0.5,"y":0.625,"width":0.25,"height":0.05,"confidence":1}])");
    const Asked found = fixture.asked(1, "vision.recognize");
    CHECK(found.json == R"({"engine":"alice.finder/boss","kind":"detect","params":{}})");
    fixture.host->answer(found.id,
                         R"([{"label":"boss","x":0.5,"y":0.5,"width":0.125,"height":0.25,"confidence":0.75}])");
    const Asked none = fixture.asked(2, "vision.recognize");
    CHECK(none.json == R"({"engine":"alice.finder/boss","kind":"classify","params":{"threshold":0.5}})");
    fixture.host->answer(none.id, "null");
    const Asked read = fixture.asked(3, "vision.recognize");
    CHECK(read.json == R"({"engine":"system","kind":"ocr","region":{"x":0,"y":0,"width":1,"height":0.5},"params":{}})");
    fixture.host->answer(read.id, "[]");
    const Asked refused = fixture.asked(4, "vision.recognize");
    CHECK(refused.json == R"({"engine":"nobody/none","kind":"detect","params":{}})");
    fixture.host->refuse(refused.id, "GS-API-170 there is no recognition engine nobody/none");
    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("lines 1 Gold 120 0.5 1 true true")) == 1);
    CHECK(fixture.recorder.count(logged("engine alice.finder/boss [object VisionEngine] true")) == 1);
    CHECK(fixture.recorder.count(logged("found 1 boss 0.125 0.75")) == 1);
    CHECK(fixture.recorder.count(logged("none null")) == 1);
    CHECK(fixture.recorder.count(logged("read 0 true")) == 1);
    CHECK(fixture.recorder.count(logged("kind GS-API-172 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("id GS-API-172 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("region GS-API-002 RangeError")) == 1);
    CHECK(fixture.recorder.count(logged("params GS-API-172 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("empty GS-API-172 TypeError")) == 1);
    CHECK(fixture.recorder.count(logged("refused GS-API-170 there is no recognition engine nobody/none")) == 1);
    CHECK(fixture.services->asks() == 5);
}

TEST_CASE("ADR-0112: a cancelled task drops its recognition, and the logic of an extension needs device.screen to "
          "recognize but not to list the engines")
{
    {
        Fixture fixture;
        const ScriptId script = fixture.load(R"js(
export async function wait() {
    await vision.recognize("system", "ocr");
}
)js");
        const TaskId task = fixture.host->start(script, "wait");
        const Asked asked = fixture.asked(0, "vision.recognize");
        fixture.host->cancel(task);
        CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
        CHECK(fixture.services->dropped() == std::vector<std::uint64_t>{asked.id});
    }
    {
        Fixture fixture;
        const ScriptId script = fixture.load(R"js(
export async function look() {
    log.info("listed", vision.engines().length);
    try {
        await vision.recognize("system", "ocr");
    } catch (error) {
        log.info("refused", error.message.split(" ")[0], error.name);
    }
}
)js",
                                             std::vector<std::string>{"device.input"});
        CHECK(fixture.finish(fixture.host->start(script, "look")).kind == ReportKind::TaskFinished);
        CHECK(fixture.recorder.count(logged("listed 2")) == 1);
        CHECK(fixture.recorder.count(logged("refused GS-API-072 TypeError")) == 1);
        CHECK(fixture.services->asks() == 0);
    }
    {
        Fixture fixture;
        const ScriptId script = fixture.load(R"js(
export async function look() {
    const lines = await vision.recognize("system", "ocr");
    log.info("allowed", lines === null);
}
)js",
                                             std::vector<std::string>{"device.screen"});
        const TaskId task = fixture.host->start(script, "look");
        fixture.host->answer(fixture.asked(0, "vision.recognize").id, "null");
        CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
        CHECK(fixture.recorder.count(logged("allowed true")) == 1);
    }
}
