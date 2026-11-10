#include "ScriptFixture.h"

#include "gs/script/Permissions.h"
#include "gs/script/Replay.h"
#include "gs/script/Services.h"

#include <gs/base/Clock.h>

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

using gs::ManualClock;
using gs::Microseconds;
using gs::script::ControlInfo;
using gs::script::DeviceInfo;
using gs::script::Point;
using gs::script::RecordingServices;
using gs::script::ReplayOptions;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::ScriptSource;
using gs::script::SessionEvent;
using gs::script::SessionEventKind;
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

class AskServices final : public gs::script::ScriptServices
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

    bool offersService(std::string_view name) override { return name == "ai.ask"; }

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
        {
            const std::scoped_lock lock(m_mutex);
            m_dropped.push_back(id);
        }
        m_changed.notify_all();
    }

    std::optional<Asked> waitForAsk(std::size_t index)
    {
        std::unique_lock lock(m_mutex);
        if (!m_changed.wait_for(lock, std::chrono::seconds(30), [&] { return m_asked.size() > index; })) {
            return std::nullopt;
        }
        return m_asked[index];
    }

    bool waitForDrop(std::uint64_t id)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::seconds(30),
                                  [&] { return std::ranges::find(m_dropped, id) != m_dropped.end(); });
    }

    std::size_t asks() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_asked.size();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<Asked> m_asked;
    std::vector<std::uint64_t> m_dropped;
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<AskServices> services = std::make_shared<AskServices>();
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

    Asked asked(std::size_t index)
    {
        const std::optional<Asked> found = services->waitForAsk(index);
        REQUIRE(found.has_value());
        CHECK(found->service == "ai.ask");
        return *found;
    }

    Report finish(TaskId task)
    {
        const auto ended = recorder.waitFor(taskEnded(task));
        REQUIRE(ended.has_value());
        return ended->report;
    }
};

constexpr std::string_view kAsking = R"js(
export async function ask() {
    try {
        log.info("answer", await ai.ask("Hello?"));
    } catch (error) {
        log.info("refused", error.message.split(" ")[0], error.name, error.message.includes("\"ai\""));
    }
}
)js";

}

TEST_CASE("SCR-022: ai.ask sends the question, the picture and the time limit through the service channel and "
          "resolves with the answer")
{
    Fixture fixture;
    fixture.folder.write("res/resources.json", R"({"format": "gamescrcpy.resources", "version": 1,
 "regions": [{"name": "Gold", "x": 0.25, "y": 0.75, "width": 0.5, "height": 0.25}]})");
    const ScriptId script = fixture.load(R"js(
export async function ask() {
    log.info("plain", JSON.stringify(await ai.ask("Is it day?")));
    log.info("whole", JSON.stringify(await ai.ask("What is on screen?", { picture: true, timeoutMs: 5000 })));
    log.info("part", await ai.ask("Score?", { picture: { x: 0.5, y: 0, width: 0.5, height: 0.25 } }));
    log.info("named", await ai.ask("Gold?", { picture: "Gold", timeoutMs: 120000 }));
    log.info("none", await ai.ask("Nothing?", { picture: false }));
    log.info("typed", typeof ai.ask, ai.ask.length, ai.ask.name, Object.isFrozen(ai));
}
)js");
    const TaskId task = fixture.host->start(script, "ask");

    const Asked plain = fixture.asked(0);
    CHECK(plain.json == R"({"question":"Is it day?","picture":false,"region":null,"timeoutMs":60000})");
    fixture.host->answer(plain.id, R"("Yes.")");
    const Asked whole = fixture.asked(1);
    CHECK(whole.json == R"({"question":"What is on screen?","picture":true,"region":null,"timeoutMs":5000})");
    fixture.host->answer(whole.id, "null");
    const Asked part = fixture.asked(2);
    CHECK(part.json == R"({"question":"Score?","picture":true,"region":{"x":0.5,"y":0,"width":0.5,"height":0.25},)"
                       R"("timeoutMs":60000})");
    fixture.host->answer(part.id, R"("12")");
    const Asked named = fixture.asked(3);
    CHECK(named.json == R"({"question":"Gold?","picture":true,"region":{"x":0.25,"y":0.75,"width":0.5,"height":0.25},)"
                        R"("timeoutMs":120000})");
    fixture.host->answer(named.id, R"("340")");
    const Asked none = fixture.asked(4);
    CHECK(none.json == R"({"question":"Nothing?","picture":false,"region":null,"timeoutMs":60000})");
    fixture.host->answer(none.id, "42");

    const Report ended = fixture.finish(task);
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged(R"(plain "Yes.")")) == 1);
    CHECK(fixture.recorder.count(logged("whole null")) == 1);
    CHECK(fixture.recorder.count(logged("part 12")) == 1);
    CHECK(fixture.recorder.count(logged("named 340")) == 1);
    CHECK(fixture.recorder.count(logged("none null")) == 1);
    CHECK(fixture.recorder.count(logged("typed function 1 ask true")) == 1);
    CHECK(fixture.services->asks() == 5);
}

TEST_CASE("SCR-022: ai.ask checks its arguments before it asks, with the codes the other functions use")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const codeOf = async (action) => {
    try {
        await action();
        return "none";
    } catch (error) {
        return error.message.split(" ")[0] + " " + error.name;
    }
};
export async function wrong() {
    log.info("number", await codeOf(() => ai.ask(5)));
    log.info("empty", await codeOf(() => ai.ask("  ")));
    log.info("long", await codeOf(() => ai.ask("x".repeat(4001))));
    log.info("options", await codeOf(() => ai.ask("Hi", 3)));
    log.info("null", await codeOf(() => ai.ask("Hi", null)));
    log.info("short", await codeOf(() => ai.ask("Hi", { timeoutMs: 999 })));
    log.info("slow", await codeOf(() => ai.ask("Hi", { timeoutMs: 120001 })));
    log.info("text", await codeOf(() => ai.ask("Hi", { timeoutMs: "5000" })));
    log.info("picture", await codeOf(() => ai.ask("Hi", { picture: 1 })));
    log.info("outside", await codeOf(() => ai.ask("Hi", { picture: { x: 0.6, y: 0, width: 0.5, height: 1 } })));
    log.info("ratio", await codeOf(() => ai.ask("Hi", { picture: { x: 2, y: 0, width: 0.1, height: 0.1 } })));
    log.info("unknown", await codeOf(() => ai.ask("Hi", { picture: "Nowhere" })));
    log.info("longest", JSON.stringify(await ai.ask("x".repeat(4000))));
}
)js");
    const TaskId task = fixture.host->start(script, "wrong");
    const Asked longest = fixture.asked(0);
    CHECK(longest.json.size() ==
          4000 + std::string_view(R"({"question":"","picture":false,"region":null,"timeoutMs":60000})").size());
    fixture.host->answer(longest.id, "null");
    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    for (const char* line : {"number GS-API-011 TypeError", "empty GS-API-011 TypeError", "long GS-API-011 TypeError",
                             "options GS-API-011 TypeError", "null GS-API-011 TypeError", "short GS-API-003 RangeError",
                             "slow GS-API-003 RangeError", "text GS-API-003 RangeError", "picture GS-API-011 TypeError",
                             "outside GS-API-002 RangeError", "ratio GS-API-002 RangeError",
                             "unknown GS-API-018 TypeError", "longest null"}) {
        CAPTURE(line);
        CHECK(fixture.recorder.count(logged(line)) == 1);
    }
    CHECK(fixture.services->asks() == 1);
}

TEST_CASE("SCR-022: the program's refusals reach the logic with their codes, and cancelling the task withdraws the "
          "question")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function refused() {
    for (let round = 0; round < 3; round += 1) {
        try {
            await ai.ask("Round " + round);
            log.info("answered", round);
        } catch (error) {
            log.info("refused", error.message);
        }
    }
}
export async function waiting() {
    await ai.ask("Take your time", { timeoutMs: 120000 });
    log.info("never");
}
)js");
    const TaskId task = fixture.host->start(script, "refused");
    const Asked first = fixture.asked(0);
    CHECK(first.json == R"({"question":"Round 0","picture":false,"region":null,"timeoutMs":60000})");
    fixture.host->refuse(first.id, R"(GS-API-095 logic may not call AI: turn on "Let logic call AI" in Settings > AI)");
    fixture.host->refuse(fixture.asked(1).id, "GS-API-096 this logic called AI 10 times in the last minute");
    fixture.host->refuse(fixture.asked(2).id, "GS-API-097 the AI service failed: it gave no answer within 60000 ms");
    CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged(
              R"(refused GS-API-095 logic may not call AI: turn on "Let logic call AI" in Settings > AI)")) == 1);
    CHECK(fixture.recorder.count(logged("refused GS-API-096 this logic called AI 10 times in the last minute")) == 1);
    CHECK(fixture.recorder.count(logged("refused GS-API-097 the AI service failed: it gave no answer within 60000 ms")) ==
          1);

    const TaskId wait = fixture.host->start(script, "waiting");
    const Asked slow = fixture.asked(3);
    CHECK(slow.json == R"({"question":"Take your time","picture":false,"region":null,"timeoutMs":120000})");
    fixture.host->cancel(wait);
    CHECK(fixture.finish(wait).kind == ReportKind::TaskCancelled);
    CHECK(fixture.services->waitForDrop(slow.id));
    CHECK(fixture.recorder.count(logged("never")) == 0);
}

TEST_CASE("SCR-022: the logic of an extension needs the permission ai to ask, the user's own logic does not")
{
    CHECK(gs::script::channelRule("ai.ask").permission == "ai");
    CHECK_FALSE(gs::script::channelRule("ai.ask").phone);
    CHECK(gs::script::channelRule("ui.render").permission.empty());

    {
        Fixture fixture;
        const ScriptId script = fixture.load(kAsking, std::vector<std::string>{"device.input", "device.screen"});
        CHECK(fixture.finish(fixture.host->start(script, "ask")).kind == ReportKind::TaskFinished);
        CHECK(fixture.recorder.count(logged("refused GS-API-072 TypeError true")) == 1);
        CHECK(fixture.services->asks() == 0);
    }
    {
        Fixture fixture;
        const ScriptId script = fixture.load(kAsking, std::vector<std::string>{"ai"});
        const TaskId task = fixture.host->start(script, "ask");
        fixture.host->answer(fixture.asked(0).id, R"("Hi")");
        CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
        CHECK(fixture.recorder.count(logged("answer Hi")) == 1);
    }
    {
        Fixture fixture;
        const ScriptId script = fixture.load(kAsking);
        const TaskId task = fixture.host->start(script, "ask");
        fixture.host->answer(fixture.asked(0).id, R"("Mine")");
        CHECK(fixture.finish(task).kind == ReportKind::TaskFinished);
        CHECK(fixture.recorder.count(logged("answer Mine")) == 1);
    }
}

TEST_CASE("SCR-022: in a replay ai.ask asks nothing, writes one line and resolves with null")
{
    ScriptFolder folder;
    folder.write("main.js", R"js(
export async function look() {
    log.info("seen", await ai.ask("Is the door open?", { picture: true }));
    log.info("said", await ai.ask("Say hi"));
}
)js");
    ManualClock clock;
    RecordingServices services(clock, DeviceInfo{"Pixel", 15, 1080, 2400, 0}, {}, {});
    const ScriptHost::Sink sink = [&services](const Report& report) {
        if (report.kind == ReportKind::Log) {
            services.note("log " + report.message);
        } else if (report.kind == ReportKind::ScriptFailed || report.kind == ReportKind::TaskFailed ||
                   report.kind == ReportKind::UnhandledRejection) {
            services.note("error " + report.message);
        }
    };
    ReplayOptions options;
    options.source = folder.source();
    options.events = {SessionEvent{.at = Microseconds{1000}, .kind = SessionEventKind::Run, .id = "look"}};
    options.until = Microseconds{1'000'000};
    const gs::script::ReplayResult result = gs::script::replay(options, services, clock, sink);
    CHECK(result.loaded);
    std::vector<std::string> lines;
    for (const gs::script::ReplayLine& line : services.lines()) {
        lines.push_back(gs::script::formatLine(line));
    }
    CHECK(lines == std::vector<std::string>{"1 ai ask (17 characters, picture)", "1 # log seen null",
                                            "1 ai ask (6 characters, no picture)", "1 # log said null"});
    CHECK(services.outputs().size() == 2);
}
