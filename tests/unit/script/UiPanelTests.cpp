#include "ScriptFixture.h"

#include "gs/script/Services.h"

#include <doctest/doctest.h>

#include <cstdint>
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
    void key(std::string_view, bool) override {}
    std::vector<ControlInfo> controls() override { return {}; }
    bool pressControl(std::string_view, bool) override { return false; }
    bool enableControl(std::string_view, bool) override { return false; }
    std::vector<StateInfo> states() override { return {}; }
    std::optional<gs::script::VarValue> stateValue(std::string_view) override { return std::nullopt; }
    bool setState(std::string_view, const gs::script::VarValue&) override { return false; }
    std::optional<std::string> loadStore(std::string_view) override { return std::nullopt; }
    void saveStore(std::string_view, std::string_view) override {}

    bool offersService(std::string_view name) override { return name.starts_with("ui."); }

    ServiceAnswer callService(std::string_view name, std::string_view json) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_calls.push_back(std::string(name) + " " + std::string(json));
        }
        if (json.find("\"refused\"") != std::string_view::npos) {
            return {"null", "GS-API-076", "alice.stats declares no panel named refused"};
        }
        return {};
    }

    std::vector<std::string> calls()
    {
        const std::scoped_lock lock(m_mutex);
        return m_calls;
    }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_calls;
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

TEST_CASE("ADR-0105: ui.panel(name).render, ui.menu, ui.status(name).render and close() reach the ui services")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const stats = ui.panel("stats");
export function run() {
    stats.render({ type: "text", text: "Rounds: 3" });
    stats.render(null);
    ui.menu([{ id: "reset", title: "Reset" }]);
    ui.status("fps").render({ type: "row", children: [{ type: "text", text: "60" }] });
    ui.status("fps").render(null);
    stats.close();
    log.info(ui.panel("stats") === stats, Object.isFrozen(stats), String(stats), String(ui.status("fps")),
             typeof ui.menu.on, ui.panel.length, ui.menu.name, Object.keys(ui).includes("panel"));
}
)js");
    const Report ended = fixture.run(script, "run");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("true true [object UiPanel] [object UiStatus] function 1 menu true")) == 1);
    CHECK(fixture.services->calls()
          == std::vector<std::string>{
              R"(ui.render {"panel":"stats","tree":{"type":"text","text":"Rounds: 3"}})",
              R"(ui.render {"panel":"stats","tree":null})",
              R"(ui.menu {"items":[{"id":"reset","title":"Reset"}]})",
              R"(ui.status {"name":"fps","tree":{"type":"row","children":[{"type":"text","text":"60"}]}})",
              R"(ui.status {"name":"fps","tree":null})",
              R"(ui.close {"panel":"stats"})",
          });
}

TEST_CASE("ADR-0105: ui.panel, ui.menu and ui.status refuse bad names, events and trees, and pass on the app's code")
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
const loop = { type: "column" };
loop.children = [loop];
export function wrong() {
    log.info(codeOf(() => ui.panel("")), codeOf(() => ui.panel(3)), codeOf(() => ui.status(null)),
             codeOf(() => ui.panel("stats").on("hover", () => {})), codeOf(() => ui.panel("stats").render()),
             codeOf(() => ui.panel("stats").render(loop)), codeOf(() => ui.menu("reset")),
             codeOf(() => ui.menu.on("click", () => {})), codeOf(() => ui.status("fps").render()),
             codeOf(() => ui.panel("stats").on("click", 5)),
             codeOf(() => ui.panel("refused").render({ type: "text", text: "x" })));
    try {
        ui.panel("refused").render({ type: "text", text: "x" });
    } catch (error) {
        log.info(error.message.includes("declares no panel named refused"));
    }
}
)js");
    const Report ended = fixture.run(script, "wrong");
    INFO(gs::script::describe(ended));
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("GS-API-011 GS-API-011 GS-API-011 GS-API-017 GS-API-075 GS-API-075 GS-API-075 "
                                        "GS-API-017 GS-API-075 GS-API-008 GS-API-076"))
          == 1);
    CHECK(fixture.recorder.count(logged("true")) == 1);
    CHECK(fixture.services->calls()
          == std::vector<std::string>(2, R"(ui.render {"panel":"refused","tree":{"type":"text","text":"x"}})"));
}

TEST_CASE("ADR-0105: an injected \"ui\" event starts the listener of that panel or of the menu, with the id and value")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
const stats = ui.panel("stats");
stats.on("click", ({ id, value }) => log.info("click", id, value));
stats.on("change", (event) => log.info("change", event.id, JSON.stringify(event.value), typeof event.signal));
ui.panel("other").on("open", ({ id }) => log.info("open other", id === ""));
const picked = ui.menu.on("select", ({ id, value }) => log.info("select", id, value));
log.info("listening", typeof picked.dispose);
export function quiet() {
    picked.dispose();
}
)js");
    CHECK(fixture.recorder.count(logged("listening function")) == 1);
    fixture.host->event("ui", "stats", R"({"type":"click","id":"go","value":null})");
    REQUIRE(fixture.recorder.waitFor(logged("click go null")).has_value());
    fixture.host->event("ui", "stats", R"({"type":"change","id":"rounds","value":{"n":3}})");
    REQUIRE(fixture.recorder.waitFor(logged(R"(change rounds {"n":3} object)")).has_value());
    fixture.host->event("ui", "other", R"({"type":"open","id":"","value":null})");
    REQUIRE(fixture.recorder.waitFor(logged("open other true")).has_value());
    fixture.host->event("ui", "menu", R"({"type":"select","id":"reset","value":true})");
    REQUIRE(fixture.recorder.waitFor(logged("select reset true")).has_value());
    fixture.host->event("ui", "stats", R"({"type":"select","id":"x","value":null})");
    fixture.host->event("ui", "stats", "3");
    fixture.host->event("ui", "nobody", R"({"type":"click","id":"go","value":null})");
    CHECK(fixture.run(script, "quiet").kind == ReportKind::TaskFinished);
    fixture.host->event("ui", "menu", R"({"type":"select","id":"again","value":null})");
    fixture.host->event("ui", "stats", R"({"type":"click","id":"last","value":1})");
    REQUIRE(fixture.recorder.waitFor(logged("click last 1")).has_value());
    CHECK(fixture.recorder.count(logged("click go null")) == 1);
    CHECK(fixture.recorder.count(logged("select again null")) == 0);
    CHECK(fixture.services->calls().empty());
}
