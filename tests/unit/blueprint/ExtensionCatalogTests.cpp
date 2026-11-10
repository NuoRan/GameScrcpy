#include "Fixture.h"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::blueprint;
using namespace blueprint_test;

namespace {

Environment withClock()
{
    nlohmann::json report = nlohmann::json::parse(readFile(std::filesystem::path(GS_SDK_DIR) / "api-report.json"));
    report["namespaces"].push_back(
        {{"name", "ext.gstest.clock"},
         {"functions",
          nlohmann::json::array({{{"name", "now"}, {"signature", "now(): string"}},
                                 {{"name", "ring"}, {"signature", "ring(times: number): void"}}})}});
    Environment made = environment();
    made.api = loadApi(report.dump(), readFile(std::filesystem::path(GS_SDK_DIR) / "types" / "gamescrcpy.d.ts"));
    made.extensions = {{"ext.gstest.clock", "Test clock"}};
    return made;
}

Environment withTools()
{
    nlohmann::json report = nlohmann::json::parse(readFile(std::filesystem::path(GS_SDK_DIR) / "api-report.json"));
    report["namespaces"].push_back(
        {{"name", "ext.gstest.tools"},
         {"functions",
          nlohmann::json::array({{{"name", "flash"},
                                  {"signature", "flash(args: { port: string; size?: number }): ExtensionJob<number>"}}})}});
    Environment made = environment();
    made.api = loadApi(report.dump(), readFile(std::filesystem::path(GS_SDK_DIR) / "types" / "gamescrcpy.d.ts"));
    made.extensions = {{"ext.gstest.tools", "Test tools"}};
    return made;
}

const Json* itemWithId(const Json& list, std::string_view id)
{
    for (const Json& item : list) {
        if (same(item["id"], id)) {
            return &item;
        }
    }
    return nullptr;
}

bool wiredToTitled(const Json& graph, const Json* from, std::string_view fromPin, std::string_view title,
                   std::string_view toPin)
{
    for (const Json& node : graph["nodes"]) {
        if (same(node["title"], title) && wired(graph, from, fromPin, &node, toPin)) {
            return true;
        }
    }
    return false;
}

std::string after(const Document& document, const Outcome& outcome)
{
    REQUIRE_MESSAGE(outcome.ok(), outcome.reason);
    return applied(document.source(), outcome.changes);
}

}

TEST_CASE("ADR-0112 a job of a native extension is a block in the extension's group, not a plain call")
{
    const Document document("log.info(1)\n", withTools(), {});
    REQUIRE(document.ok());
    const Json all = document.catalog("", "", "", "");
    const Json* flash = itemWithId(all, "job:ext.gstest.tools.flash");
    REQUIRE(flash != nullptr);
    CHECK((*flash)["title"] == "flash");
    CHECK((*flash)["group"] == "Test tools");
    CHECK((*flash)["icon"] == "loader-circle");
    CHECK(itemWithId(all, "call:ext.gstest.tools.flash") == nullptr);
    CHECK(itemWithId(all, "pure:ext.gstest.tools.flash") == nullptr);
}

TEST_CASE("ADR-0112 a followed job is one block with its values as inputs and progress, done and failed exits")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await ext.gstest.tools.flash({ port: \"COM9\" }).follow(async (fraction, text) => {\n"
                               "    log.info(text)\n"
                               "  }, async (result) => {\n"
                               "    log.info(result)\n"
                               "  }, async (error) => {\n"
                               "    log.info(error)\n"
                               "  })\n"
                               "  await sleep(100)\n"
                               "})\n";
    const Document document(source, withTools(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    const Json* job = nodeTitled(graph, "flash");
    REQUIRE(job != nullptr);
    CHECK((*job)["badge"] == "任务");
    CHECK((*job)["icon"] == "loader-circle");
    REQUIRE(pinNamed(*job, "inputs", "args.port") != nullptr);
    CHECK((*pinNamed(*job, "inputs", "args.port"))["value"] == "COM9");
    CHECK_FALSE((*pinNamed(*job, "inputs", "args.port"))["optional"].get<bool>());
    REQUIRE(pinNamed(*job, "inputs", "args.size") != nullptr);
    CHECK((*pinNamed(*job, "inputs", "args.size"))["optional"].get<bool>());
    std::vector<std::string> outputs;
    for (const Json& pin : (*job)["outputs"]) {
        outputs.push_back(pin["name"].get<std::string>());
    }
    CHECK(outputs == std::vector<std::string>{"progress", "fraction", "text", "done", "result", "failed", "error", "then"});
    CHECK((*pinNamed(*job, "outputs", "progress"))["label"] == "进度");
    CHECK((*pinNamed(*job, "outputs", "failed"))["label"] == "失败");
    CHECK((*pinNamed(*job, "outputs", "fraction"))["label"] == "比例");
    CHECK((*pinNamed(*job, "outputs", "text"))["label"] == "状态");
    CHECK((*pinNamed(*job, "outputs", "error"))["label"] == "错误");
    CHECK(wiredToTitled(graph, job, "progress", "log.info", "exec"));
    CHECK(wiredToTitled(graph, job, "text", "log.info", "values"));
    CHECK(wiredToTitled(graph, job, "done", "log.info", "exec"));
    CHECK(wiredToTitled(graph, job, "result", "log.info", "values"));
    CHECK(wiredToTitled(graph, job, "failed", "log.info", "exec"));
    CHECK(wiredToTitled(graph, job, "error", "log.info", "values"));
    CHECK(wiredToTitled(graph, job, "then", "等待", "exec"));
    CHECK(nodeTitled(graph, "代码块") == nullptr);
}

TEST_CASE("ADR-0112 adding a job block writes the call with its required values and the three exits, and a block "
          "taken from a value of an exit goes into that exit")
{
    const Document start("keys.F.onPress(async () => {\n  await sleep(100)\n})\n", withTools(), {});
    const Json first = start.graph("");
    const std::string wait = keyOf(nodeTitled(first, "等待"));
    const std::string text = after(start, start.add("", "job:ext.gstest.tools.flash", 400, 40, wait, "exec"));
    CHECK(text == "keys.F.onPress(async () => {\n"
                  "  await sleep(100)\n"
                  "  await ext.gstest.tools.flash({ port: \"\" }).follow(async (fraction, text) => {\n"
                  "  }, async (result) => {\n"
                  "  }, async (error) => {\n"
                  "  })\n"
                  "})\n");

    const Document next(text, withTools(), {}, &start);
    REQUIRE(next.ok());
    const Json graph = next.graph("");
    const Json* job = nodeTitled(graph, "flash");
    REQUIRE(job != nullptr);
    CHECK(wiredToTitled(graph, nodeTitled(graph, "等待"), "exec", "flash", "exec"));

    CHECK(after(next, next.add("", "call:log.info", 600, 40, keyOf(job), "error"))
          == "keys.F.onPress(async () => {\n"
             "  await sleep(100)\n"
             "  await ext.gstest.tools.flash({ port: \"\" }).follow(async (fraction, text) => {\n"
             "  }, async (result) => {\n"
             "  }, async (error) => {\n"
             "    log.info()\n"
             "  })\n"
             "})\n");
}

TEST_CASE("ADR-0097 the system nodes of native extensions are blocks grouped under the extension that adds them")
{
    const Document document("log.info(1)\n", withClock(), {});
    REQUIRE(document.ok());
    const Json all = document.catalog("", "", "", "");
    const Json* now = itemWithId(all, "pure:ext.gstest.clock.now");
    const Json* ring = itemWithId(all, "call:ext.gstest.clock.ring");
    REQUIRE(now != nullptr);
    REQUIRE(ring != nullptr);
    CHECK((*now)["title"] == "now");
    CHECK((*now)["subtitle"] == "ext.gstest.clock.now");
    CHECK((*now)["group"] == "Test clock");
    CHECK((*ring)["title"] == "ring");
    CHECK((*ring)["group"] == "Test clock");
    CHECK((*ring)["icon"] == "plug");

    std::vector<std::string> found;
    for (const Json& item : document.catalog("", "", "", "test clock")) {
        found.push_back(item["id"].get<std::string>());
    }
    CHECK(found == std::vector<std::string>{"pure:ext.gstest.clock.now", "call:ext.gstest.clock.ring"});

    const Document plain("log.info(1)\n", environment(), {});
    CHECK(itemWithId(plain.catalog("", "", "", ""), "pure:ext.gstest.clock.now") == nullptr);
}

TEST_CASE("ADR-0097 a call to a native extension's node reads back as that node's block with its values")
{
    const Document document("keys.F.onPress(() => {\n  ext.gstest.clock.ring(3)\n})\n", withClock(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    const Json* ring = nodeTitled(graph, "ring");
    REQUIRE(ring != nullptr);
    CHECK((*ring)["icon"] == "plug");
    REQUIRE(pinNamed(*ring, "inputs", "times") != nullptr);
    CHECK((*pinNamed(*ring, "inputs", "times"))["raw"] == 3);
    CHECK(nodeTitled(graph, "ext.gstest.clock.ring") == nullptr);
}
