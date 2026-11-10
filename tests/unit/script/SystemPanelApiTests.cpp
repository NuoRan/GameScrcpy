#include "ScriptFixture.h"

#include "gs/script/Replay.h"
#include "gs/script/Services.h"
#include "gs/script/Session.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using gs::ManualClock;
using gs::Microseconds;
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
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

class PanelServices final : public gs::script::ScriptServices
{
public:
    DeviceInfo device() override { return {"Pixel", 15, 1080, 2400, 0}; }
    void tap(std::uint64_t, Point, double) override {}
    void swipe(std::uint64_t, Point, Point, double) override {}
    void key(std::string_view name, bool down) override
    {
        record("key " + std::string(name) + (down ? " down" : " up"));
    }
    std::vector<ControlInfo> controls() override { return {}; }
    bool pressControl(std::string_view, bool) override { return false; }
    bool enableControl(std::string_view, bool) override { return false; }
    std::vector<StateInfo> states() override { return {}; }
    std::optional<gs::script::VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const gs::script::VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view) override {}

    bool offersService(std::string_view service) override { return service == "panel.show"; }

    ServiceAnswer callService(std::string_view service, std::string_view json) override
    {
        record(std::string(service) + " " + std::string(json));
        const std::scoped_lock lock(m_mutex);
        return m_answer;
    }

    void answerWith(ServiceAnswer answer)
    {
        const std::scoped_lock lock(m_mutex);
        m_answer = std::move(answer);
    }

    std::vector<std::string> calls()
    {
        const std::scoped_lock lock(m_mutex);
        return m_calls;
    }

private:
    void record(std::string call)
    {
        const std::scoped_lock lock(m_mutex);
        m_calls.push_back(std::move(call));
    }

    std::mutex m_mutex;
    std::vector<std::string> m_calls;
    ServiceAnswer m_answer{"true", {}, {}};
};

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::shared_ptr<PanelServices> services = std::make_shared<PanelServices>();
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

TEST_CASE("ADR-0105: device.notifications, quickSettings and collapsePanels ask panel.show for their panel")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export function run() {
    log.info(device.notifications.length, device.quickSettings.length, device.collapsePanels.length,
             device.collapsePanels.name);
    log.info(device.notifications(), device.quickSettings(), device.collapsePanels());
}
)js");
    const Report ended = fixture.run(script, "run");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("0 0 0 collapsePanels")) == 1);
    CHECK(fixture.recorder.count(logged("true true true")) == 1);
    CHECK(fixture.services->calls() == std::vector<std::string>{R"(panel.show {"kind":"notifications"})",
                                                                R"(panel.show {"kind":"settings"})",
                                                                R"(panel.show {"kind":"collapse"})"});
}

TEST_CASE("ADR-0105: a phone that cannot be asked now gives false, and refusals reach the logic with their codes")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export function later() {
    log.info("later", device.quickSettings());
}
export function refused() {
    try {
        device.notifications();
        log.info("not refused");
    } catch (error) {
        log.info(error.name, error.message.split(" ")[0]);
    }
}
)js");
    fixture.services->answerWith({"false", {}, {}});
    CHECK(fixture.run(script, "later").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("later false")) == 1);

    fixture.services->answerWith({"null", "GS-API-091", "the phone is connected without the GameScrcpy server"});
    CHECK(fixture.run(script, "refused").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("TypeError GS-API-091")) == 1);

    fixture.services->answerWith({"null", "GS-API-090", "this logic does not run on a phone"});
    CHECK(fixture.run(script, "refused").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("TypeError GS-API-090")) == 1);
    CHECK(fixture.recorder.count(logged("not refused")) == 0);
    CHECK(fixture.services->calls() == std::vector<std::string>{R"(panel.show {"kind":"settings"})",
                                                                R"(panel.show {"kind":"notifications"})",
                                                                R"(panel.show {"kind":"notifications"})"});
}

TEST_CASE("ADR-0105: replays record the panels the logic asks for")
{
    ScriptFolder folder;
    folder.write("main.js", R"js(
export function run() {
    log.info(device.notifications(), device.quickSettings(), device.collapsePanels());
}
)js");
    ManualClock clock;
    gs::script::RecordingServices services(clock, DeviceInfo{"Pixel", 15, 1080, 2400, 0}, {}, {});
    const ScriptHost::Sink sink = [&services](const Report& report) {
        if (report.kind == ReportKind::Log) {
            services.note("log " + report.message);
        }
    };
    gs::script::ReplayOptions options;
    options.source = folder.source();
    options.events = {{Microseconds{0}, gs::script::SessionEventKind::Run, "run", false}};
    options.until = Microseconds{1'000'000};
    static_cast<void>(gs::script::replay(options, services, clock, sink));
    std::vector<std::string> lines;
    for (const auto& line : services.lines()) {
        lines.push_back(gs::script::formatLine(line));
    }
    CHECK(lines == std::vector<std::string>{"0 panel notifications", "0 panel settings", "0 panel collapse",
                                            "0 # log true true true"});
}
