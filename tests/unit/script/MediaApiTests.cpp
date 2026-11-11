#include "ScriptFixture.h"

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
#include <thread>
#include <utility>
#include <vector>

using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::Point;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::ServiceAnswer;
using gs::script::StateInfo;
using gs::script::TaskId;
using gs::script::test::kindIs;
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

class MediaServices final : public gs::script::ScriptServices
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

    bool offersService(std::string_view name) override { return name.starts_with("media."); }

    ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_calls.push_back(std::string(name) + " " + std::string(json));
        }
        m_changed.notify_all();
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

    void dropService(std::uint64_t) override {}

    std::optional<Asked> waitForAsk(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_asked.size() > index; })) {
            return std::nullopt;
        }
        return m_asked[index];
    }

    bool waitForCalls(const std::string& call, std::size_t count)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::seconds(30), [&] {
            return static_cast<std::size_t>(std::ranges::count(m_calls, call)) >= count;
        });
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

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<Asked> m_asked;
    std::vector<std::string> m_calls;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<MediaServices> services = std::make_shared<MediaServices>();
    std::unique_ptr<ScriptHost> host = std::make_unique<ScriptHost>(recorder.sink(), gs::script::Limits{}, services);

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

    static std::string frame(double time, int capture)
    {
        return R"({"type":"frame","time":)" + std::to_string(time) + R"(,"image":{"capture":)" + std::to_string(capture) +
               R"(,"x":0.5,"y":0,"width":0.5,"height":1,"pixelWidth":284,"pixelHeight":320}})";
    }
};

}

TEST_CASE("ADR-0112: media.frames asks for frames with its options, hands each frame to its handler as a task with "
          "the image and the time, and says when the handler is done")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function watch() {
    const subscription = media.frames(async ({ image, time, signal }) => {
        log.info("frame", time, image.width, image.height, image.region.x, image.region.width,
                 Object.prototype.toString.call(image), typeof image.save, Object.isFrozen(image), signal.aborted);
        await sleep(100);
        log.info("handled", time);
    }, { fps: 5, size: 320, region: { x: 0.5, y: 0, width: 0.5, height: 1 } });
    log.info("subscribed", typeof subscription.dispose, Object.isFrozen(subscription));
    await sleep(30000);
}
)js");
    const TaskId task = fixture.host->start(script, "watch");
    const Asked ask = fixture.asked(0, "media.frames");
    CHECK(ask.json == R"({"fps":5,"size":320,"region":{"x":0.5,"y":0,"width":0.5,"height":1}})");
    REQUIRE(fixture.recorder.waitFor(logged("subscribed function true")).has_value());
    fixture.host->answer(ask.id, R"({"handle":"f1"})");

    fixture.host->event("media", "f1", Fixture::frame(40, 3));
    REQUIRE(fixture.recorder.waitFor(logged("frame 40 284 320 0.5 0.5 [object CapturedImage] function true false"))
                .has_value());
    fixture.host->event("media", "f1", Fixture::frame(80, 4));
    REQUIRE(fixture.recorder.waitFor(logged("handled 40")).has_value());
    REQUIRE(fixture.services->waitForCalls(R"(media.done {"handle":"f1"})", 2));
    CHECK(fixture.recorder.count(logged("handled 80")) == 0);
    CHECK(fixture.recorder.count([](const Report& report) {
        return report.kind == ReportKind::Log && report.message.starts_with("frame 80");
    }) == 0);

    fixture.host->event("media", "f1", Fixture::frame(120, 5));
    REQUIRE(fixture.recorder.waitFor(logged("handled 120")).has_value());
    REQUIRE(fixture.services->waitForCalls(R"(media.done {"handle":"f1"})", 3));
    fixture.host->event("media", "f2", Fixture::frame(160, 6));
    fixture.host->event("media", "f1", R"({"type":"closed","reason":"ended"})");
    fixture.host->event("media", "f1", Fixture::frame(200, 7));

    fixture.host->cancel(task);
    CHECK(fixture.finish(task).kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(logged("handled 160")) == 0);
    CHECK(fixture.recorder.count(logged("handled 200")) == 0);
    CHECK(fixture.services->calls() == std::vector<std::string>(3, R"(media.done {"handle":"f1"})"));
}

TEST_CASE("ADR-0112: a media.frames subscription stops when it is disposed or its task ends, also before GameScrcpy "
          "answered, and its options are checked")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
let kept;
export async function keep() {
    kept = media.frames(() => log.info("kept frame"));
    log.info("keeping");
    await sleep(30000);
}
export function drop() {
    kept.dispose();
    log.info("dropped");
}
export async function early() {
    const subscription = media.frames(() => log.info("early frame"));
    subscription.dispose();
    log.info("early gone");
    await sleep(30000);
}
export function wrong() {
    for (const [what, options] of [["fps", { fps: 0.05 }], ["fast", { fps: 11 }], ["size", { size: 50 }],
                                   ["whole", { size: 100.5 }], ["region", { region: { x: 0.7, y: 0, width: 0.5, height: 1 } }],
                                   ["object", 5]]) {
        try {
            media.frames(() => undefined, options);
        } catch (error) {
            log.info(what, error.message.split(" ")[0], error.name);
        }
    }
    try {
        media.frames(5);
    } catch (error) {
        log.info("handler", error.message.split(" ")[0]);
    }
}
)js");
    const TaskId keeping = fixture.host->start(script, "keep");
    const Asked first = fixture.asked(0, "media.frames");
    CHECK(first.json == R"({"fps":2,"size":640,"region":null})");
    REQUIRE(fixture.recorder.waitFor(logged("keeping")).has_value());
    fixture.host->answer(first.id, R"({"handle":"f1"})");
    fixture.host->event("media", "f1", Fixture::frame(10, 1));
    REQUIRE(fixture.recorder.waitFor(logged("kept frame")).has_value());
    REQUIRE(fixture.services->waitForCalls(R"(media.done {"handle":"f1"})", 1));
    const TaskId dropping = fixture.host->start(script, "drop");
    CHECK(fixture.finish(dropping).kind == ReportKind::TaskFinished);
    REQUIRE(fixture.services->waitForCalls(R"(media.close {"handle":"f1"})", 1));
    fixture.host->event("media", "f1", Fixture::frame(20, 2));

    const TaskId early = fixture.host->start(script, "early");
    const Asked second = fixture.asked(1, "media.frames");
    REQUIRE(fixture.recorder.waitFor(logged("early gone")).has_value());
    fixture.host->answer(second.id, R"({"handle":"f2"})");
    REQUIRE(fixture.services->waitForCalls(R"(media.close {"handle":"f2"})", 1));
    fixture.host->event("media", "f2", Fixture::frame(30, 3));

    const TaskId wrong = fixture.host->start(script, "wrong");
    CHECK(fixture.finish(wrong).kind == ReportKind::TaskFinished);
    for (const char* line : {"fps GS-API-120 RangeError", "fast GS-API-120 RangeError", "size GS-API-120 RangeError",
                             "whole GS-API-120 RangeError", "region GS-API-120 RangeError", "object GS-API-120 TypeError",
                             "handler GS-API-008"}) {
        CAPTURE(line);
        CHECK(fixture.recorder.count(logged(line)) == 1);
    }
    CHECK(fixture.services->asks() == 2);

    fixture.host->cancel(keeping);
    fixture.host->cancel(early);
    CHECK(fixture.finish(keeping).kind == ReportKind::TaskCancelled);
    CHECK(fixture.finish(early).kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(logged("kept frame")) == 1);
    CHECK(fixture.recorder.count(logged("early frame")) == 0);
}

TEST_CASE("ADR-0112: a media.frames that GameScrcpy refuses reports the refusal, and its task-made subscription "
          "closes with the task")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function refused() {
    media.frames(() => log.info("never"));
    await sleep(30000);
}
export async function brief() {
    media.frames(() => log.info("brief frame"));
    await sleep(200);
}
)js");
    const TaskId refusedTask = fixture.host->start(script, "refused");
    fixture.host->refuse(fixture.asked(0, "media.frames").id, "GS-API-121 the phone shows no picture to take frames from");
    const auto rejection = fixture.recorder.waitFor(kindIs(ReportKind::UnhandledRejection));
    REQUIRE(rejection.has_value());
    CHECK(rejection->report.message.find("GS-API-121") != std::string::npos);

    const TaskId briefTask = fixture.host->start(script, "brief");
    fixture.host->answer(fixture.asked(1, "media.frames").id, R"({"handle":"f9"})");
    CHECK(fixture.finish(briefTask).kind == ReportKind::TaskFinished);
    REQUIRE(fixture.services->waitForCalls(R"(media.close {"handle":"f9"})", 1));
    fixture.host->event("media", "f9", Fixture::frame(50, 1));
    fixture.host->cancel(refusedTask);
    CHECK(fixture.finish(refusedTask).kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(logged("brief frame")) == 0);
    CHECK(fixture.recorder.count(logged("never")) == 0);
}
