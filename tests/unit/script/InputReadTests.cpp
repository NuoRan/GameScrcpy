#include "ScriptFixture.h"

#include "gs/script/Replay.h"
#include "gs/script/Services.h"

#include <gs/base/Clock.h>

#include <doctest/doctest.h>

#include <atomic>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
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
using gs::script::VarValue;
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

class ReadServices final : public gs::script::ScriptServices
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
    std::optional<VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view) override {}

    std::optional<Point> cursor() override
    {
        record("cursor");
        return Point{0.25, 0.75};
    }

    bool keyDown(std::string_view name) override
    {
        record(std::format("keyDown {}", name));
        return name == "KeyW";
    }

    bool readsInput() override
    {
        record("readsInput");
        return m_allowed.load();
    }

    void allow() { m_allowed.store(true); }

    [[nodiscard]] std::vector<std::string> events() const
    {
        const std::scoped_lock lock(m_mutex);
        return m_events;
    }

    void clear()
    {
        const std::scoped_lock lock(m_mutex);
        m_events.clear();
    }

private:
    void record(std::string event)
    {
        const std::scoped_lock lock(m_mutex);
        m_events.push_back(std::move(event));
    }

    mutable std::mutex m_mutex;
    std::vector<std::string> m_events;
    std::atomic<bool> m_allowed{false};
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<ReadServices> services = std::make_shared<ReadServices>();
    ScriptHost host{recorder.sink(), gs::script::Limits{}, services};

    ScriptId load(std::string_view main)
    {
        folder.write("main.js", main);
        ScriptId id = 0;
        const std::optional<Report> report = loadScript(host, recorder, folder.source(), &id);
        REQUIRE(report.has_value());
        INFO(gs::script::describe(*report));
        REQUIRE(report->kind == ReportKind::ScriptLoaded);
        return id;
    }

    Report run(ScriptId script, const std::string& handler)
    {
        const TaskId task = host.start(script, handler);
        const auto ended = recorder.waitFor(taskEnded(task));
        REQUIRE(ended.has_value());
        return ended->report;
    }
};

constexpr std::string_view kReader = R"js(
const codeOf = (action) => {
    try {
        action();
        return "none";
    } catch (error) {
        return error.name + " " + error.message.split(" ")[0];
    }
};
export function refused() {
    log.info(codeOf(() => input.cursor()), codeOf(() => input.isDown("KeyW")), codeOf(() => input.isDown(3)));
}
export function read() {
    try {
        const at = input.cursor();
        log.info("read", at.x, at.y, input.isDown("KeyW"), input.isDown("KeyS"));
    } catch (error) {
        log.info("refused", error.message);
    }
}
)js";

}

TEST_CASE("SCR-019: input.cursor and input.isDown throw GS-API-098 while the logic may not read input, and never ask "
          "for the cursor or the keys")
{
    Fixture fixture;
    const ScriptId script = fixture.load(kReader);
    const Report ended = fixture.run(script, "refused");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("TypeError GS-API-098 TypeError GS-API-098 TypeError GS-API-013")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"readsInput", "readsInput"});

    CHECK(fixture.run(script, "read").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("refused GS-API-098 this logic may not read your keys and cursor yet; allow "
                                        "it in the mirror window")) == 1);
}

TEST_CASE("SCR-019: once the host lets the logic read input, input.cursor and input.isDown read the cursor and the "
          "keys without loading the logic again")
{
    Fixture fixture;
    const ScriptId script = fixture.load(kReader);
    CHECK(fixture.run(script, "read").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("read 0.25 0.75 true false")) == 0);

    fixture.services->allow();
    fixture.services->clear();
    CHECK(fixture.run(script, "read").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("read 0.25 0.75 true false")) == 1);
    CHECK(fixture.services->events() == std::vector<std::string>{"readsInput", "cursor", "readsInput", "keyDown KeyW",
                                                                 "readsInput", "keyDown KeyS"});
}

TEST_CASE("SCR-019: replays read input without asking anyone, as before")
{
    gs::ManualClock clock;
    gs::script::RecordingServices services(clock, DeviceInfo{"Pixel", 15, 1080, 2400, 0}, {}, {});
    CHECK(services.readsInput());
    CHECK_FALSE(services.cursor().has_value());
    CHECK_FALSE(services.keyDown("KeyW"));
}
