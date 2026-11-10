#include "Fixture.h"
#include "Scopes.h"
#include "Syntax.h"

#include <gs/blueprint/Text.h>
#include <gs/tsstrip/Strip.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../support/ThreadCpuTime.h"

using namespace gs::blueprint;
using namespace blueprint_test;

namespace {

const Json& inputPin(const Json& node, std::string_view name)
{
    const Json* found = pinNamed(node, "inputs", name);
    REQUIRE(found != nullptr);
    return *found;
}

std::vector<std::filesystem::path> scriptsUnder(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ts") {
            found.push_back(entry.path());
        }
    }
    return found;
}

Document c4(const Layout& layout = {}, const Document* previous = nullptr)
{
    return Document(fixture("c4.ts"), environment(), layout, previous);
}

std::string replaced(std::string text, std::string_view from, std::string_view to)
{
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

std::string replacedAll(std::string text, std::string_view from, std::string_view to)
{
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

std::array<double, 2> positionOf(const Document& document, const std::string& key)
{
    const Json graph = document.graph("");
    for (const Json& node : graph["nodes"]) {
        if (node["key"] == key) {
            return {node["x"].get<double>(), node["y"].get<double>()};
        }
    }
    FAIL("node not found: " << key);
    return {};
}

const Json* nodeStartingWith(const Json& graph, std::string_view prefix)
{
    for (const Json& node : graph["nodes"]) {
        if (node["key"].get<std::string>().starts_with(prefix)) {
            return &node;
        }
    }
    return nullptr;
}

bool pageListed(const Json& pages, std::string_view key)
{
    return std::ranges::any_of(pages, [key](const Json& page) { return same(page["key"], key); });
}

}

TEST_CASE("BP-001 every fixture, example and test script parses without problems and stays byte-identical")
{
    std::vector<std::filesystem::path> files = scriptsUnder(GS_EXAMPLES_DIR);
    for (const std::filesystem::path& path : scriptsUnder(GS_SCRIPTS_DATA_DIR)) {
        files.push_back(path);
    }
    files.push_back(dataDir() / "c4.ts");
    files.push_back(dataDir() / "unsupported.ts");
    REQUIRE(files.size() >= 13);
    for (const std::filesystem::path& path : files) {
        const std::string source = readFile(path);
        CAPTURE(path.string());
        const Document document(source, environment(path.filename().string()), {});
        CHECK_MESSAGE(document.ok(), document.problem());
        CHECK(document.source() == source);
        CHECK(applied(source, {}) == source);
        bool drawn = false;
        for (const Json& page : document.pages()) {
            const Json graph = document.graph(page["key"].get<std::string>());
            CHECK(graph["problem"] == "");
            drawn = drawn || !graph["nodes"].empty();
        }
        CHECK(drawn);
    }
}

TEST_CASE("BP-002 a syntax error makes the whole file one problem with its line and no nodes")
{
    const Document document("keys.F.onTap(() => {\n  touch.tap(0.5, 0.5\n})\n", environment(), {});
    CHECK_FALSE(document.ok());
    CHECK(document.problemLine() == 3);
    const Json graph = document.graph("");
    CHECK(graph["nodes"].empty());
    CHECK(graph["readOnly"] == true);
    CHECK(graph["problemLine"] == 3);
    CHECK(graph["problem"].get<std::string>().starts_with("代码第 3 行有语法错误"));
    const Json pages = document.pages();
    REQUIRE(pages.size() == 1);
    CHECK(pages[0]["title"] == "主页");
}

TEST_CASE("BP-003 the C4 code draws exactly the C3 graph")
{
    const Document document = c4();
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    CHECK(graph["kind"] == "events");
    CHECK(graph["title"] == "主页");
    std::size_t events = 0;
    for (const Json& node : graph["nodes"]) {
        if (node["kind"] == "event") {
            ++events;
        }
    }
    CHECK(events == 5);
    CHECK(nodeTitled(graph, "载入时") == nullptr);

    const Json* tap = nodeTitled(graph, "按键 F", "单击");
    const Json* hold = nodeTitled(graph, "按键 F", "长按 0.5 秒");
    const Json* press = nodeTitled(graph, "按键 左键", "按下");
    const Json* release = nodeTitled(graph, "按键 左键", "松开");
    const Json* every = nodeTitled(graph, "每隔", "0.5 秒");
    REQUIRE(tap != nullptr);
    REQUIRE(hold != nullptr);
    REQUIRE(press != nullptr);
    REQUIRE(release != nullptr);
    REQUIRE(every != nullptr);
    CHECK((*tap)["kind"] == "event");
    CHECK((*tap)["tone"] == "event");
    CHECK((*tap)["note"] == "F：单击拾取，长按开背包");
    CHECK((*press)["note"] == "开镜时左键连发");
    CHECK((*every)["note"] == "看到“拾取”就点");
    CHECK((*tap)["inputs"].empty());

    const Json* pick = nodeTitled(graph, "拾取 · 点一下");
    const Json* bag = nodeTitled(graph, "背包 · 点一下");
    const Json* branch = nodeTitled(graph, "判断");
    const Json* aiming = nodeTitled(graph, "开镜 · 按着中");
    const Json* burst = nodeTitled(graph, "连发 · 开始");
    const Json* fire = nodeTitled(graph, "开火 · 按下");
    const Json* stop = nodeTitled(graph, "连发 · 停止");
    const Json* lift = nodeTitled(graph, "开火 · 松开");
    const Json* find = nodeTitled(graph, "找图");
    const Json* touchAt = nodeTitled(graph, "点某处");
    for (const Json* node : {pick, bag, branch, aiming, burst, fire, stop, lift, find, touchAt}) {
        REQUIRE(node != nullptr);
    }
    CHECK(graph["nodes"].size() == 15);
    CHECK((*pick)["kind"] == "action");
    CHECK((*branch)["kind"] == "flow");
    CHECK((*aiming)["kind"] == "pure");
    CHECK((*find)["tone"] == "vision");
    CHECK((*find)["kind"] == "flow");

    CHECK(wired(graph, tap, "exec", pick, "exec"));
    CHECK(wired(graph, hold, "exec", bag, "exec"));
    CHECK(wired(graph, press, "exec", branch, "exec"));
    CHECK(wired(graph, aiming, "value", branch, "condition"));
    CHECK(wired(graph, branch, "true", burst, "exec"));
    CHECK(wired(graph, branch, "false", fire, "exec"));
    CHECK(wired(graph, release, "exec", stop, "exec"));
    CHECK(wired(graph, stop, "exec", lift, "exec"));
    CHECK(wired(graph, every, "exec", find, "exec"));
    CHECK(wired(graph, find, "found", touchAt, "exec"));
    CHECK(wired(graph, find, "位置", touchAt, "x"));
    CHECK(graph["wires"].size() == 11);

    const Json* condition = pinNamed(*branch, "inputs", "condition");
    REQUIRE(condition != nullptr);
    CHECK((*condition)["label"] == "条件");
    CHECK((*condition)["type"] == "bool");
    CHECK((*condition)["connected"] == true);
    const Json* then = pinNamed(*branch, "outputs", "then");
    REQUIRE(then != nullptr);
    CHECK((*then)["optional"] == true);
    CHECK((*then)["connected"] == false);
    CHECK((*pinNamed(*branch, "outputs", "true"))["label"] == "是");
    CHECK((*pinNamed(*branch, "outputs", "false"))["label"] == "否");

    CHECK((*pinNamed(*find, "outputs", "found"))["label"] == "找到");
    CHECK((*pinNamed(*find, "outputs", "missing"))["label"] == "没找到");
    const Json* position = pinNamed(*find, "outputs", "位置");
    REQUIRE(position != nullptr);
    CHECK((*position)["label"] == "位置");
    CHECK((*position)["type"] == "point");
    const Json* image = pinNamed(*find, "inputs", "image");
    REQUIRE(image != nullptr);
    CHECK((*image)["label"] == "图片");
    CHECK((*image)["type"] == "image");
    CHECK((*image)["value"] == "拾取");
    CHECK((*image)["editor"] == "image");
    CHECK((*image)["preview"] == "file:///res/pick.png");
    const Json* x = pinNamed(*touchAt, "inputs", "x");
    REQUIRE(x != nullptr);
    CHECK((*x)["type"] == "point");
    CHECK((*x)["label"] == "位置");
    CHECK((*x)["connected"] == true);
}

TEST_CASE("BP-004 literals become pin values: points, durations, toggles, text, choices and code")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await touch.tap(0.5, 0.3)\n"
                               "  await touch.tap({ x: 0.25, y: 0.75 }, undefined, 80)\n"
                               "  await sleep(1500)\n"
                               "  controls.移动.enabled = false\n"
                               "  device.rotate(\"portrait\")\n"
                               "  log.info(`at ${Date.now()}`, 3)\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    std::vector<const Json*> taps;
    for (const Json& node : graph["nodes"]) {
        if (node["title"] == "点某处") {
            taps.push_back(&node);
        }
    }
    REQUIRE(taps.size() == 2);
    const Json& first = *pinNamed(*taps[0], "inputs", "x");
    CHECK(first["value"] == "50%, 30%");
    CHECK(first["raw"]["x"] == 0.5);
    CHECK(first["raw"]["y"] == 0.3);
    CHECK(first["editor"] == "point");
    const Json& second = *pinNamed(*taps[1], "inputs", "x");
    CHECK(second["value"] == "25%, 75%");
    const Json& duration = *pinNamed(*taps[1], "inputs", "durationMs");
    CHECK(duration["type"] == "duration");
    CHECK(duration["raw"] == 80);
    CHECK(duration["value"] == "0.08 秒");
    CHECK(duration["optional"] == true);
    const Json* wait = nodeTitled(graph, "等待");
    REQUIRE(wait != nullptr);
    CHECK((*wait)["tone"] == "wait");
    CHECK((*pinNamed(*wait, "inputs", "ms"))["value"] == "1.5 秒");
    const Json* set = nodeTitled(graph, "设置 移动 · 开着");
    REQUIRE(set != nullptr);
    const Json& on = *pinNamed(*set, "inputs", "value");
    CHECK(on["type"] == "bool");
    CHECK(on["raw"] == false);
    CHECK(on["value"] == "否");
    CHECK(on["editor"] == "toggle");
    const Json* rotate = nodeTitled(graph, "device.rotate");
    REQUIRE(rotate != nullptr);
    const Json& orientation = *pinNamed(*rotate, "inputs", "orientation");
    CHECK(orientation["editor"] == "choice");
    CHECK(orientation["choices"].size() == 3);
    CHECK(orientation["raw"] == "portrait");
    const Json* info = nodeTitled(graph, "log.info");
    REQUIRE(info != nullptr);
    const Json& message = *pinNamed(*info, "inputs", "values");
    CHECK(message["editor"] == "code");
    CHECK(message["value"] == "`at ${Date.now()}`");
    CHECK((*pinNamed(*info, "inputs", "values2"))["raw"] == 3);
    CHECK(pinNamed(*info, "inputs", "values3") != nullptr);
}

TEST_CASE("BP-004 a named point of the profile is a pin value, with its duration after it")
{
    const Document document("keys.F.onPress(async () => {\n  await touch.tap(res.point(\"入口\"), undefined, 80)\n})\n",
                            environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    const Json* tap = nodeTitled(graph, "点某处");
    REQUIRE(tap != nullptr);
    const Json& at = *pinNamed(*tap, "inputs", "x");
    CHECK(at["raw"] == "入口");
    CHECK(at["value"] == "入口");
    CHECK(at["editor"] == "point");
    CHECK((*pinNamed(*tap, "inputs", "durationMs"))["raw"] == 80);
}

TEST_CASE("BP-004 a swipe reads every form by its meaning: four numbers, two points, the spaced form and mixes (issue 12)")
{
    const Document document("keys.F.onPress(async () => {\n"
                            "  await touch.swipe(0.5, 0.8, 0.5, 0.2, 250)\n"
                            "  await touch.swipe(res.point(\"起点\"), res.point(\"终点\"), 300)\n"
                            "  await touch.swipe(res.point(\"起点\"), undefined, res.point(\"终点\"), undefined, 400)\n"
                            "  await touch.swipe(res.point(\"起点\"), undefined, 0.5, 0.2, 500)\n"
                            "  await touch.swipe(0.1, 0.2, res.point(\"终点\"), undefined, 600)\n"
                            "})\n",
                            environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    std::vector<const Json*> swipes;
    for (const Json& node : graph["nodes"]) {
        if (same(node["title"], "touch.swipe")) {
            swipes.push_back(&node);
        }
    }
    REQUIRE(swipes.size() == 5);
    for (const Json* swipe : swipes) {
        CAPTURE((*swipe)["key"].get<std::string>());
        CHECK(same((*swipe)["problem"], ""));
        CHECK(inputPin(*swipe, "from")["type"] == "point");
        CHECK(inputPin(*swipe, "to")["type"] == "point");
    }
    CHECK(inputPin(*swipes[0], "from")["raw"]["x"] == 0.5);
    CHECK(inputPin(*swipes[0], "from")["raw"]["y"] == 0.8);
    CHECK(inputPin(*swipes[0], "to")["raw"]["x"] == 0.5);
    CHECK(inputPin(*swipes[0], "to")["raw"]["y"] == 0.2);
    CHECK(inputPin(*swipes[0], "durationMs")["raw"] == 250);
    CHECK(inputPin(*swipes[1], "from")["raw"] == "起点");
    CHECK(inputPin(*swipes[1], "to")["raw"] == "终点");
    CHECK(inputPin(*swipes[1], "durationMs")["raw"] == 300);
    CHECK(inputPin(*swipes[2], "from")["raw"] == "起点");
    CHECK(inputPin(*swipes[2], "to")["raw"] == "终点");
    CHECK(inputPin(*swipes[2], "durationMs")["raw"] == 400);
    CHECK(inputPin(*swipes[3], "from")["raw"] == "起点");
    CHECK(inputPin(*swipes[3], "to")["raw"]["x"] == 0.5);
    CHECK(inputPin(*swipes[3], "to")["raw"]["y"] == 0.2);
    CHECK(inputPin(*swipes[3], "durationMs")["raw"] == 500);
    CHECK(inputPin(*swipes[4], "from")["raw"]["x"] == 0.1);
    CHECK(inputPin(*swipes[4], "from")["raw"]["y"] == 0.2);
    CHECK(inputPin(*swipes[4], "to")["raw"] == "终点");
    CHECK(inputPin(*swipes[4], "durationMs")["raw"] == 600);
}

TEST_CASE("BP-004 a swipe that passes values the runtime ignores is marked, and a fixed line is not (issue 12)")
{
    const Document broken("keys.F.onPress(async () => {\n  await touch.swipe(res.point(\"点位 10\"), "
                          "res.point(\"点位 6\"), 0.5, 0.3, 10)\n})\n",
                          environment(), {});
    REQUIRE(broken.ok());
    const Json graph = broken.graph("");
    const Json* swipe = nodeTitled(graph, "touch.swipe");
    REQUIRE(swipe != nullptr);
    CHECK((*swipe)["problem"] == "This call passes values that are not used");
    CHECK(inputPin(*swipe, "from")["raw"] == "点位 10");
    CHECK(inputPin(*swipe, "to")["raw"] == "点位 6");
    CHECK(inputPin(*swipe, "durationMs")["raw"] == 0.5);

    const Document fixed("keys.F.onPress(async () => {\n  await touch.swipe(res.point(\"点位 10\"), undefined, "
                         "res.point(\"点位 6\"), undefined, 250)\n})\n",
                         environment(), {});
    REQUIRE(fixed.ok());
    const Json fixedGraph = fixed.graph("");
    const Json* fixedSwipe = nodeTitled(fixedGraph, "touch.swipe");
    REQUIRE(fixedSwipe != nullptr);
    CHECK(same((*fixedSwipe)["problem"], ""));
    CHECK(inputPin(*fixedSwipe, "durationMs")["raw"] == 250);
}

TEST_CASE("BP-004 a tap with a point reads its duration second or third, and marks the value it ignores (issue 12)")
{
    const Document document("keys.F.onPress(async () => {\n"
                            "  await touch.tap(res.point(\"入口\"), 80)\n"
                            "  await touch.tap(res.point(\"入口\"), undefined, 90)\n"
                            "  await touch.tap(0.5, 0.5, 100)\n"
                            "})\n",
                            environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    std::vector<const Json*> taps;
    for (const Json& node : graph["nodes"]) {
        if (same(node["title"], "点某处")) {
            taps.push_back(&node);
        }
    }
    REQUIRE(taps.size() == 3);
    for (const Json* tap : taps) {
        CAPTURE((*tap)["key"].get<std::string>());
        CHECK(same((*tap)["problem"], ""));
    }
    CHECK(inputPin(*taps[0], "durationMs")["raw"] == 80);
    CHECK(inputPin(*taps[1], "durationMs")["raw"] == 90);
    CHECK(inputPin(*taps[2], "durationMs")["raw"] == 100);
    const Document extra("keys.F.onPress(async () => {\n  await touch.tap(res.point(\"入口\"), 80, 100)\n})\n",
                         environment(), {});
    const Json extraGraph = extra.graph("");
    const Json* tap = nodeTitled(extraGraph, "点某处");
    REQUIRE(tap != nullptr);
    CHECK((*tap)["problem"] == "This call passes values that are not used");
    CHECK(inputPin(*tap, "durationMs")["raw"] == 80);
    CHECK(inputPin(*tap, "x")["raw"] == "入口");
}

TEST_CASE("BP-004 options that are not written show the declared default in the details, written ones their value")
{
    const Document document("keys.F.onPress(async () => {\n  await vision.waitFor(\"拾取\")\n"
                            "  await vision.waitFor(\"拾取\", { timeoutMs: 3000 })\n})\n",
                            environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    std::vector<std::string> waits;
    for (const Json& node : graph["nodes"]) {
        if (same(node["title"], "vision.waitFor")) {
            waits.push_back(node["key"].get<std::string>());
        }
    }
    REQUIRE(waits.size() == 2);
    const auto fieldOf = [&document](const std::string& key, std::string_view name) {
        const Json details = document.details("", key);
        for (const Json& group : details["groups"]) {
            for (const Json& field : group["fields"]) {
                if (same(field["name"], name)) {
                    return field;
                }
            }
        }
        return Json();
    };
    const Json unset = fieldOf(waits[0], "options.timeoutMs");
    REQUIRE(unset.is_object());
    CHECK(unset["kind"] == "duration");
    CHECK(unset["isDefault"] == true);
    CHECK(unset["value"] == 10000);
    const Json threshold = fieldOf(waits[0], "options.threshold");
    REQUIRE(threshold.is_object());
    CHECK(threshold["isDefault"] == true);
    CHECK(threshold["value"] == "");
    const Json written = fieldOf(waits[1], "options.timeoutMs");
    REQUIRE(written.is_object());
    CHECK_FALSE(written.contains("isDefault"));
    CHECK(written["value"] == 3000);
    const Json interval = fieldOf(waits[1], "options.intervalMs");
    REQUIRE(interval.is_object());
    CHECK(interval["isDefault"] == true);
    CHECK(interval["value"] == 250);
}

TEST_CASE("BP-005 structures, loops, terminals, code blocks and readers of locals")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  let total = 0\n"
                               "  for (let i = 0; i < 3; i++) {\n"
                               "    if (i > 1) break\n"
                               "    total = total + i\n"
                               "  }\n"
                               "  for (const name of [\"a\", \"b\"]) log.info(name)\n"
                               "  while (total > 0) {\n"
                               "    total = total - 1\n"
                               "    continue\n"
                               "  }\n"
                               "  total++\n"
                               "  switch (total) { default: break }\n"
                               "  const { a } = { a: 1 }\n"
                               "  return total\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    const Json* repeat = nodeTitled(graph, "重复");
    REQUIRE(repeat != nullptr);
    CHECK((*pinNamed(*repeat, "inputs", "times"))["raw"] == 3);
    CHECK(pinNamed(*repeat, "outputs", "i") != nullptr);
    CHECK(nodeTitled(graph, "跳出循环") != nullptr);
    CHECK(nodeTitled(graph, "下一次") != nullptr);
    const Json* each = nodeTitled(graph, "对每个");
    REQUIRE(each != nullptr);
    const Json* info = nodeTitled(graph, "log.info");
    REQUIRE(info != nullptr);
    CHECK(wired(graph, each, "each", info, "exec"));
    CHECK(wired(graph, each, "name", info, "values"));
    CHECK(nodeTitled(graph, "当…时重复") != nullptr);
    CHECK(countTitled(graph, "代码块") == 2);
    const Json* code = nodeTitled(graph, "代码块");
    REQUIRE(code != nullptr);
    CHECK((*code)["code"] == "total++");
    const Json* branch = nodeTitled(graph, "多分支");
    REQUIRE(branch != nullptr);
    CHECK((*pinNamed(*branch, "outputs", "default"))["label"] == "其他");
    CHECK(nodeTitled(graph, "返回") != nullptr);
    const Json* returned = nodeTitled(graph, "返回");
    REQUIRE(returned != nullptr);
    CHECK((*returned)["kind"] == "return");
    CHECK(countTitled(graph, "取 total") >= 3);
    CHECK(countTitled(graph, "设置 total") == 3);
}

TEST_CASE("issue 31: a switch draws one output per case and the default, and its bodies stay inside it")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  let pick = 1\n"
                               "  switch (pick) {\n"
                               "    case 1:\n"
                               "      await sleep(100)\n"
                               "      break\n"
                               "    case 2:\n"
                               "    case 3:\n"
                               "      await sleep(200)\n"
                               "      break\n"
                               "    default:\n"
                               "      controls.开火.press()\n"
                               "  }\n"
                               "  await touch.tap(0.5, 0.5)\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    const Json* branch = nodeTitled(graph, "多分支");
    REQUIRE(branch != nullptr);
    CHECK((*branch)["kind"] == "flow");
    CHECK((*branch)["outputs"].size() == 5);
    std::size_t exits = 0;
    for (const Json& pin : (*branch)["outputs"]) {
        if (pin["name"] != "then") {
            ++exits;
        }
    }
    CHECK(exits == 4);
    CHECK((*pinNamed(*branch, "outputs", "case:0"))["label"] == "1");
    CHECK((*pinNamed(*branch, "outputs", "case:1"))["label"] == "2");
    CHECK((*pinNamed(*branch, "outputs", "case:2"))["label"] == "3");
    CHECK((*pinNamed(*branch, "outputs", "default"))["label"] == "其他");
    CHECK(pinNamed(*branch, "outputs", "then") != nullptr);
    const Json* value = pinNamed(*branch, "inputs", "value");
    REQUIRE(value != nullptr);
    const Json* read = nodeTitled(graph, "取 pick");
    REQUIRE(read != nullptr);
    bool fed = false;
    for (const Json& wire : graph["wires"]) {
        fed = fed || (wire["from"] == (*read)["key"] && wire["to"] == (*branch)["key"] && wire["toPin"] == "value");
    }
    CHECK(fed);
    CHECK(countTitled(graph, "跳出循环") == 0);
    const auto waitWith = [&graph](std::string_view shown) -> const Json* {
        for (const Json& node : graph["nodes"]) {
            if (!same(node["title"], "等待")) {
                continue;
            }
            const Json* ms = pinNamed(node, "inputs", "ms");
            if (ms != nullptr && same((*ms)["value"], shown)) {
                return &node;
            }
        }
        return nullptr;
    };
    const Json* waits = waitWith("0.1 秒");
    const Json* waits2 = waitWith("0.2 秒");
    REQUIRE(waits != nullptr);
    REQUIRE(waits2 != nullptr);
    CHECK(wired(graph, branch, "case:0", waits, "exec"));
    CHECK(wired(graph, branch, "case:2", waits2, "exec"));
    CHECK(wired(graph, branch, "then", nodeTitled(graph, "点某处"), "exec"));
    CHECK(wired(graph, branch, "default", nodeTitled(graph, "开火 · 按下"), "exec"));

    const Json details = document.details("", (*branch)["key"].get<std::string>(), {});
    bool cases = false;
    for (const Json& group : details["groups"]) {
        for (const Json& field : group["fields"]) {
            if (field["name"] == "cases") {
                cases = true;
                CHECK(field["value"] == "1, 2, 3");
            }
        }
    }
    CHECK(cases);
}

TEST_CASE("issue 31: sequence, multiGate, a range loop and a loop with an index are blocks with their own outputs")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await sequence(async () => {\n"
                               "    await sleep(100)\n"
                               "  }, async () => {\n"
                               "    await sleep(200)\n"
                               "  })\n"
                               "  await multiGate(\"door\", 2, false, false, async () => {\n"
                               "    controls.开火.press()\n"
                               "  }, async () => {\n"
                               "    controls.开火.release()\n"
                               "  }, async () => {\n"
                               "    controls.背包.tap()\n"
                               "  })\n"
                               "  for (let step = 1; step <= 3; step++) {\n"
                               "    await touch.tap(0.5, 0.5)\n"
                               "  }\n"
                               "  for (const [row, name] of [\"a\", \"b\"].entries()) {\n"
                               "    log.info(row, name)\n"
                               "  }\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    const auto waitWith = [&graph](std::string_view shown) -> const Json* {
        for (const Json& node : graph["nodes"]) {
            if (!same(node["title"], "等待")) {
                continue;
            }
            const Json* ms = pinNamed(node, "inputs", "ms");
            if (ms != nullptr && same((*ms)["value"], shown)) {
                return &node;
            }
        }
        return nullptr;
    };
    const Json* order = nodeTitled(graph, "顺序");
    REQUIRE(order != nullptr);
    CHECK((*order)["outputs"].size() == 3);
    CHECK(wired(graph, order, "1", waitWith("0.1 秒"), "exec"));
    CHECK(wired(graph, order, "2", waitWith("0.2 秒"), "exec"));

    const Json* gate = nodeTitled(graph, "多路门");
    REQUIRE(gate != nullptr);
    CHECK((*gate)["outputs"].size() == 4);
    CHECK((*pinNamed(*gate, "outputs", "3"))["label"] == "做完");
    CHECK((*pinNamed(*gate, "inputs", "name"))["value"] == "door");
    CHECK((*pinNamed(*gate, "inputs", "times"))["raw"] == 2);
    CHECK(wired(graph, gate, "1", nodeTitled(graph, "开火 · 按下"), "exec"));
    CHECK(wired(graph, gate, "2", nodeTitled(graph, "开火 · 松开"), "exec"));

    const Json* range = nodeTitled(graph, "按范围重复");
    REQUIRE(range != nullptr);
    CHECK((*range)["badge"] == "<=");
    CHECK((*pinNamed(*range, "inputs", "from"))["raw"] == 1);
    CHECK((*pinNamed(*range, "inputs", "to"))["raw"] == 3);
    CHECK(pinNamed(*range, "outputs", "step") != nullptr);
    CHECK(wired(graph, range, "each", nodeTitled(graph, "点某处"), "exec"));

    const Json* indexed = nodeTitled(graph, "带序号的对每个");
    REQUIRE(indexed != nullptr);
    CHECK(pinNamed(*indexed, "outputs", "row") != nullptr);
    CHECK(pinNamed(*indexed, "outputs", "name") != nullptr);
    const Json* info = nodeTitled(graph, "log.info");
    REQUIRE(info != nullptr);
    CHECK(wired(graph, indexed, "row", info, "values"));
    CHECK(wired(graph, indexed, "name", info, "values2"));
}

TEST_CASE("issue 31: the catalog has the list, dictionary, set, text and math blocks in their own groups")
{
    const Document document = c4();
    const Json list = document.catalog("", "", "", "shuffle");
    REQUIRE(list.size() == 1);
    CHECK(list[0]["id"] == "call:list.shuffle");
    CHECK(list[0]["group"] == "列表");
    const Json size = document.catalog("", "", "", "list.size");
    REQUIRE(size.size() == 1);
    CHECK(size[0]["id"] == "pure:list.size");
    CHECK(size[0]["group"] == "列表");
    const Json has = document.catalog("", "", "", "list.has");
    REQUIRE(has.size() == 1);
    CHECK(has[0]["id"] == "pure:list.has");
    const Json dictionary = document.catalog("", "", "", "dict.make");
    REQUIRE(dictionary.size() == 1);
    CHECK(dictionary[0]["id"] == "pure:dict.make");
    CHECK(dictionary[0]["group"] == "字典");
    const Json setItems = document.catalog("", "", "", "set.values");
    REQUIRE(setItems.size() == 1);
    CHECK(setItems[0]["id"] == "pure:set.values");
    CHECK(setItems[0]["group"] == "集合");
    const Json textBlocks = document.catalog("", "", "", "text.format");
    REQUIRE(textBlocks.size() == 1);
    CHECK(textBlocks[0]["id"] == "pure:text.format");
    CHECK(textBlocks[0]["group"] == "文字");
    const Json rounded = document.catalog("", "", "", "math.round");
    REQUIRE(rounded.size() == 1);
    CHECK(rounded[0]["id"] == "pure:math.round");
    CHECK(rounded[0]["group"] == "数学");
    const Json blocks = document.catalog("", "", "", "multi-branch");
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0]["id"] == "flow:switch");
    CHECK(blocks[0]["group"] == "流程");
    bool doN = false;
    for (const Json& item : document.catalog("", "", "", "doN")) {
        doN = doN || item["id"] == "pure:doN";
    }
    CHECK(doN);
    const Json order = document.catalog("", "", "", "sequence");
    REQUIRE(order.size() == 1);
    CHECK(order[0]["id"] == "flow:sequence");
    const Json gate = document.catalog("", "", "", "multi-gate");
    REQUIRE(gate.size() == 1);
    CHECK(gate[0]["id"] == "flow:multiGate");
}

TEST_CASE("BP-006 handlers that are not inline, component files, setup, overrides and super calls")
{
    const std::string logic = "keys.F.onTap(attack)\n"
                              "async function attack(times: number) {\n"
                              "  await touch.tap(0.1, 0.2)\n"
                              "}\n";
    const Document first(logic, environment(), {});
    REQUIRE(first.ok());
    const Json graph = first.graph("");
    const Json* key = nodeTitled(graph, "按键 F");
    REQUIRE(key != nullptr);
    CHECK((*key)["badge"] == "\xE2\x86\x92 attack");
    CHECK(first.open(keyOf(key)) == "fn:attack");
    const Json pages = first.pages();
    REQUIRE(pages.size() == 2);
    CHECK(pages[0]["key"] == "");
    CHECK(pageListed(pages, "fn:attack"));
    CHECK_FALSE(pageListed(pages, "control:开火"));
    const Json function = first.graph("fn:attack");
    CHECK(function["kind"] == "function");
    const Json* entry = nodeTitled(function, "attack");
    REQUIRE(entry != nullptr);
    CHECK((*entry)["kind"] == "entry");
    CHECK(pinNamed(*entry, "outputs", "times") != nullptr);
    CHECK(wired(function, entry, "exec", nodeTitled(function, "点某处"), "exec"));

    const Document component(readFile(std::filesystem::path(GS_EXAMPLES_DIR) / "components" / "toggle-hold" / "component.ts"),
                             environment("component.ts"), {});
    REQUIRE(component.ok());
    const Json events = component.graph("");
    CHECK(nodeTitled(events, "准备好时") != nullptr);
    const Json* own = nodeTitled(events, "Key", "按下");
    REQUIRE(own != nullptr);
    CHECK((*own)["kind"] == "event");
    const Json sidebar = component.sidebar();
    REQUIRE(sidebar.size() == 7);
    CHECK(sidebar[0]["key"] == "props");
    CHECK(sidebar[0]["items"][0]["title"] == "Key");
    CHECK(sidebar[1]["items"].size() == 2);
    CHECK(sidebar[3]["items"][0]["title"] == "holding");

    const std::string derived = "export default class Flash extends Hold {\n"
                                "  onPressed() {\n"
                                "    super.onPressed()\n"
                                "    log.info(this.position)\n"
                                "  }\n"
                                "  helper() {}\n"
                                "}\n";
    const Document override(derived, environment("component.ts"), {});
    REQUIRE(override.ok());
    const Json page = override.graph("");
    const Json* event = nodeTitled(page, "按下时", "覆盖");
    REQUIRE(event != nullptr);
    const Json* parent = nodeTitled(page, "调用父类 · 按下时");
    REQUIRE(parent != nullptr);
    CHECK((*parent)["kind"] == "flow");
    CHECK(wired(page, event, "exec", parent, "exec"));
    CHECK(nodeTitled(page, "自己 · 位置") != nullptr);
    CHECK(override.hasPage("fn:Flash.helper"));
}

TEST_CASE("BP-007 the catalog lists what can connect: exec output, bool output, point input, component pin")
{
    const Document document = c4();
    const Json graph = document.graph("");
    const std::string pick = keyOf(nodeTitled(graph, "拾取 · 点一下"));
    const Json statements = document.catalog("", pick, "exec", "");
    REQUIRE(!statements.empty());
    bool tap = false;
    bool ifBlock = false;
    for (const Json& item : statements) {
        CHECK_FALSE(item["id"].get<std::string>().starts_with("event:"));
        CHECK_FALSE(item["id"].get<std::string>().starts_with("op:"));
        CHECK_FALSE(item["id"].get<std::string>().starts_with("get:"));
        tap = tap || item["id"] == "call:touch.tap";
        ifBlock = ifBlock || item["id"] == "flow:if";
    }
    CHECK(tap);
    CHECK(ifBlock);
    CHECK(statements.front()["group"] == "组件");

    const std::string aiming = keyOf(nodeTitled(graph, "开镜 · 按着中"));
    const Json accepting = document.catalog("", aiming, "value", "");
    bool branch = false;
    bool both = false;
    for (const Json& item : accepting) {
        CHECK_FALSE(item["id"].get<std::string>().starts_with("event:keys."));
        branch = branch || item["id"] == "flow:if";
        both = both || item["id"] == "op:&&";
    }
    CHECK(branch);
    CHECK(both);

    const std::string touchAt = keyOf(nodeTitled(graph, "点某处"));
    const Json producing = document.catalog("", touchAt, "x", "");
    bool find = false;
    bool point = false;
    for (const Json& item : producing) {
        CHECK(item["id"] != "call:touch.tap");
        find = find || item["id"] == "call:vision.find";
        point = point || item["id"] == "pure:point";
    }
    CHECK(find);
    CHECK(point);

    const Document pill("keys.F.onPress(() => log.info(controls.开镜))\n", environment(), {});
    const Json pillGraph = pill.graph("");
    const std::string reference = keyOf(nodeTitled(pillGraph, "开镜"));
    REQUIRE(!reference.empty());
    const Json members = pill.catalog("", reference, "value", "");
    REQUIRE(!members.empty());
    std::vector<std::string> groups;
    for (const Json& item : members) {
        const std::string group = item["group"].get<std::string>();
        if (groups.empty() || groups.back() != group) {
            groups.push_back(group);
        }
        CHECK(item["id"].get<std::string>().find("controls.开镜.") != std::string::npos);
    }
    CHECK(groups == std::vector<std::string>{"属性", "状态", "事件", "函数"});
    bool holding = false;
    for (const Json& item : members) {
        if (item["title"] == "开镜 · 按着中") {
            holding = item["group"] == "状态";
        }
    }
    CHECK(holding);
}

TEST_CASE("BP-008 catalog search matches the translated title, the English title, the API name and keywords")
{
    const Document document = c4();
    const auto ids = [&document](std::string_view query) {
        std::vector<std::string> found;
        for (const Json& item : document.catalog("", "", "", query)) {
            found.push_back(item["id"].get<std::string>());
        }
        return found;
    };
    const auto has = [](const std::vector<std::string>& list, std::string_view id) {
        return std::ranges::find(list, id) != list.end();
    };
    CHECK(has(ids("找图"), "call:vision.find"));
    CHECK(has(ids("find image"), "call:vision.find"));
    CHECK(has(ids("VISION.FIND"), "call:vision.find"));
    CHECK(has(ids("点某处"), "call:touch.tap"));
    CHECK_FALSE(has(ids("点某处"), "call:vision.find"));
    CHECK(has(ids("拾取"), "call:controls.拾取.tap"));
    const std::vector<std::string> all = ids("");
    CHECK(all.size() > 100);
    CHECK(all.front().starts_with("event:"));
}

TEST_CASE("BP-050 a component lists only the blocks that do something for it")
{
    const Document document = c4();
    const Json list = document.catalog("", "", "", "");
    const auto has = [&list](std::string_view id) {
        for (const Json& item : list) {
            if (item["id"].get<std::string>() == std::string(id)) {
                return true;
            }
        }
        return false;
    };
    CHECK(has("call:controls.移动.push"));
    CHECK(has("call:controls.移动.walkFor"));
    CHECK(has("call:controls.移动.release"));
    CHECK(has("get:controls.移动.moving"));
    CHECK(has("get:controls.移动.direction"));
    CHECK_FALSE(has("call:controls.移动.press"));
    CHECK_FALSE(has("call:controls.移动.hold"));
    CHECK_FALSE(has("call:controls.移动.trigger"));
    CHECK_FALSE(has("call:controls.移动.enable"));
    CHECK_FALSE(has("call:controls.移动.disable"));
    CHECK_FALSE(has("call:controls.移动.toggle"));
    CHECK_FALSE(has("pure:controls.移动.child"));
    CHECK_FALSE(has("get:controls.移动.touching"));
    CHECK(has("call:controls.开镜.holdFor"));
    CHECK(has("call:controls.开镜.press"));
    CHECK(has("get:controls.开镜.holding"));
    CHECK(has("get:controls.开镜.active"));
    CHECK(has("event:controls.开镜.onTouchStart"));
    CHECK_FALSE(has("call:controls.开镜.hold"));
    CHECK_FALSE(has("get:controls.开镜.touching"));
    CHECK_FALSE(has("event:controls.开镜.onPressed"));
    CHECK_FALSE(has("event:controls.开镜.onReleased"));

    const std::string source = "keys.F.onPress(async () => {\n"
                               "  controls.移动.press()\n"
                               "  controls.移动.hold(200)\n"
                               "  await controls.移动.trigger()\n"
                               "  controls.移动.enable()\n"
                               "})\n";
    const Document written(source, environment(), {});
    REQUIRE(written.ok());
    const Json graph = written.graph("");
    CHECK(nodeTitled(graph, "移动 · 按下") != nullptr);
    CHECK(nodeTitled(graph, "移动 · 按住一会儿") != nullptr);
    CHECK(nodeTitled(graph, "移动 · 点一下再松开") != nullptr);
    CHECK(nodeTitled(graph, "移动 · 打开") != nullptr);
}

TEST_CASE("BP-051 every component type leaves out the blocks that do nothing for it")
{
    Environment all;
    all.api = api();
    all.labels = &labels();
    all.fileName = "logic.ts";
    all.controls = {{"t", "t", "Tap"},           {"h", "h", "Hold"},           {"lp", "lp", "LongPress"},
                    {"tu", "tu", "Turbo"},       {"sw", "sw", "Swipe"},        {"pi", "pi", "Pinch"},
                    {"wasd", "wasd", "Wasd"},    {"pad", "pad", "PadStick"},   {"rw", "rw", "RightWalk"},
                    {"cam", "cam", "Camera"},    {"pcam", "pcam", "PadCamera"}, {"fl", "fl", "FreeLook"},
                    {"sk", "sk", "Skill"},       {"dl", "dl", "Dial"},         {"mp", "mp", "MultiPress"},
                    {"tc", "tc", "TempCursor"},  {"aw", "aw", "ActionWheel"}};
    const Document document("log.info(1)\n", all, {});
    const Json list = document.catalog("", "", "", "");
    const auto present = [&list](std::string_view id) {
        for (const Json& item : list) {
            if (item["id"].get<std::string>() == std::string(id)) {
                return true;
            }
        }
        return false;
    };
    const auto absent = [&present](std::string_view id) { return !present(id); };
    for (const Json& item : list) {
        if (item["id"] == "call:controls.t.tap") {
            CHECK(item["controlTitle"] == "t");
        }
        if (item["id"] == "call:touch.tap") {
            CHECK(item["controlTitle"] == "");
        }
    }

    CHECK(present("call:controls.t.tap"));
    CHECK(absent("call:controls.t.trigger"));
    CHECK(present("call:controls.h.holdFor"));
    CHECK(absent("call:controls.h.hold"));
    CHECK(absent("event:controls.h.onPressed"));
    CHECK(present("event:controls.h.onTouchStart"));
    CHECK(absent("call:controls.lp.hold"));
    CHECK(absent("call:controls.lp.trigger"));
    CHECK(absent("event:controls.lp.onStart"));
    CHECK(present("call:controls.lp.longPress"));
    CHECK(absent("call:controls.tu.hold"));
    CHECK(absent("call:controls.tu.trigger"));
    CHECK(present("call:controls.tu.burst"));
    CHECK(absent("call:controls.sw.trigger"));
    CHECK(absent("call:controls.sw.press"));
    CHECK(absent("event:controls.sw.onStart"));
    CHECK(present("call:controls.sw.swipe"));
    CHECK(absent("call:controls.pi.trigger"));
    CHECK(absent("call:controls.pi.press"));
    CHECK(present("call:controls.pi.zoomIn"));
    CHECK(absent("call:controls.wasd.trigger"));
    CHECK(absent("call:controls.wasd.hold"));
    CHECK(absent("call:controls.wasd.press"));
    CHECK(present("call:controls.wasd.release"));
    CHECK(present("call:controls.wasd.push"));
    CHECK(absent("call:controls.pad.press"));
    CHECK(absent("get:controls.pad.active"));
    CHECK(absent("event:controls.pad.onPress"));
    CHECK(present("get:controls.pad.moving"));
    CHECK(absent("call:controls.rw.press"));
    CHECK(absent("call:controls.rw.release"));
    CHECK(present("call:controls.rw.walkTo"));
    CHECK(absent("call:controls.cam.press"));
    CHECK(absent("get:controls.cam.active"));
    CHECK(absent("get:controls.cam.key"));
    CHECK(absent("event:controls.cam.onTouchStart"));
    CHECK(present("event:controls.cam.onTurnStart"));
    CHECK(present("call:controls.cam.lock"));
    CHECK(absent("call:controls.pcam.press"));
    CHECK(absent("get:controls.pcam.key"));
    CHECK(absent("call:controls.pcam.lock"));
    CHECK(present("event:controls.pcam.onTurnStart"));
    CHECK(present("get:controls.fl.key"));
    CHECK(absent("call:controls.fl.press"));
    CHECK(absent("call:controls.fl.release"));
    CHECK(absent("event:controls.fl.onStart"));
    CHECK(present("call:controls.fl.begin"));
    CHECK(present("call:controls.fl.end"));
    CHECK(absent("call:controls.sk.trigger"));
    CHECK(absent("call:controls.sk.press"));
    CHECK(present("call:controls.sk.aim"));
    CHECK(present("call:controls.sk.cast"));
    CHECK(present("call:controls.sk.cancel"));
    CHECK(absent("call:controls.dl.trigger"));
    CHECK(absent("call:controls.dl.press"));
    CHECK(absent("event:controls.dl.onTouchStart"));
    CHECK(present("event:controls.dl.onOpen"));
    CHECK(present("call:controls.dl.choose"));
    CHECK(absent("call:controls.mp.hold"));
    CHECK(absent("event:controls.mp.onPressed"));
    CHECK(present("call:controls.mp.holdFor"));
    CHECK(present("call:controls.mp.trigger"));
    CHECK(absent("call:controls.tc.trigger"));
    CHECK(absent("event:controls.tc.onTouchStart"));
    CHECK(present("event:controls.tc.onShow"));
    CHECK(present("call:controls.tc.press"));
    CHECK(absent("call:controls.aw.press"));
    CHECK(absent("call:controls.aw.trigger"));
    CHECK(absent("event:controls.aw.onTouchStart"));
    CHECK(present("call:controls.aw.choose"));
    CHECK(present("event:controls.aw.onChoose"));
}

TEST_CASE("BP-009 the layout file round-trips, ignores corrupt or newer files and is written in a fixed format")
{
    Layout layout;
    layout.pages[""].nodes["e:keys.F.onTap#0"] = {32, 44};
    layout.pages[""].view = ViewState{10, 20, 1.5};
    layout.pages[""].comments.push_back({"c1", 16, 14, 446, 200, "F：单击拾取，长按开背包", "blue"});
    layout.pages[""].reroutes.push_back({"r1", 200, 90, "a", "exec", "b", "exec"});
    layout.pages["f:attack"].floating.push_back({"f1", "await touch.tap(0.5, 0.5)", 300, 400});
    layout.breakpoints.push_back({"e:every#0/exec/find:vision.find#0", false});
    const std::string text = writeLayout(layout);
    CHECK(text.starts_with("{\n  \"format\": \"gamescrcpy.blueprint\",\n  \"version\": 1,"));
    CHECK(parseLayout(text) == layout);
    CHECK(parseLayout("{not json").pages.empty());
    CHECK(parseLayout("{\"format\": \"gamescrcpy.blueprint\", \"version\": 2, \"pages\": {\"\": {}}}").pages.empty());
    CHECK(parseLayout("{\"format\": \"other\", \"version\": 1, \"pages\": {\"\": {}}}").pages.empty());
    CHECK(parseLayout("[]").pages.empty());
    CHECK(layoutFileName("logic.ts") == "logic.blueprint.json");
    CHECK(layoutFileName("src/main.ts") == "src/main.blueprint.json");
    const Document broken(readFile(dataDir() / "c4.ts"), environment(), parseLayout("{\"format\":\"gamescrcpy.blueprint\""));
    CHECK(broken.ok());
    CHECK(!broken.graph("")["nodes"].empty());
}

TEST_CASE("BP-010 positions survive a statement inserted above, an edit of another handler and a renamed local")
{
    const Document first = c4();
    const std::string pick = keyOf(nodeTitled(first.graph(""), "拾取 · 点一下"));
    const std::string find = keyOf(nodeTitled(first.graph(""), "找图"));
    const Outcome moved = first.move("", {pick, find}, 300, 120);
    const std::array<double, 2> pickAt = positionOf(Document(first.source(), environment(), moved.layout), pick);
    const std::array<double, 2> findAt = positionOf(Document(first.source(), environment(), moved.layout), find);

    const std::string above = replaced(first.source(), "// F：", "log.info(\"start\")\n\n// F：");
    const Document inserted(above, environment(), moved.layout, &first);
    CHECK(positionOf(inserted, pick) == pickAt);
    CHECK(positionOf(inserted, find) == findAt);
    CHECK(nodeTitled(inserted.graph(""), "载入时") != nullptr);

    const std::string other = replaced(first.source(), "  controls.连发.stop()\n", "  controls.连发.stop()\n  controls.开火.press()\n");
    const Document edited(other, environment(), moved.layout, &first);
    CHECK(positionOf(edited, pick) == pickAt);
    CHECK(positionOf(edited, find) == findAt);

    const std::string renamed = replacedAll(first.source(), "位置", "目标");
    const Document local(renamed, environment(), moved.layout, &first);
    CHECK(positionOf(local, find) == findAt);
    const Json localGraph = local.graph("");
    const Json* output = pinNamed(*nodeTitled(localGraph, "找图"), "outputs", "目标");
    CHECK(output != nullptr);

    const std::string swapped = replaced(first.source(), "keys.F.onTap(() => controls.拾取.tap())\n", "")
                              + "keys.F.onTap(() => controls.拾取.tap())\n";
    const Document reordered(swapped, environment(), moved.layout, &first);
    CHECK(positionOf(reordered, pick) == pickAt);
}

TEST_CASE("BP-011 breakpoints are kept by anchor, can be turned off, and are invalid outside async functions")
{
    const Document document = c4();
    const Json graph = document.graph("");
    const std::string touchAt = keyOf(nodeTitled(graph, "点某处"));
    const std::string burst = keyOf(nodeTitled(graph, "连发 · 开始"));
    const Outcome on = document.toggleBreakpoint(touchAt);
    REQUIRE(on.layout.breakpoints.size() == 1);
    CHECK(on.layout.breakpoints[0].anchor == touchAt);
    const Document stopped(document.source(), environment(), on.layout);
    CHECK((*nodeTitled(stopped.graph(""), "点某处"))["breakpoint"] == "on");
    const Outcome off = stopped.enableBreakpoint(touchAt, false);
    const Document paused(document.source(), environment(), off.layout);
    CHECK((*nodeTitled(paused.graph(""), "点某处"))["breakpoint"] == "off");
    const Json positions = paused.breakpointPositions();
    REQUIRE(positions.size() == 1);
    CHECK(positions[0]["position"] == "21:11");
    CHECK(positions[0]["enabled"] == false);
    CHECK(paused.nodeAt("21:11") == touchAt);

    const Outcome invalid = paused.toggleBreakpoint(burst);
    const Document sync(document.source(), environment(), invalid.layout);
    const Json syncGraph = sync.graph("");
    const Json* node = nodeTitled(syncGraph, "连发 · 开始");
    REQUIRE(node != nullptr);
    CHECK((*node)["breakpoint"] == "invalid");
    CHECK((*node)["breakpointReason"] == "只能停在 async 函数里");
    CHECK(sync.breakpointPositions().size() == 1);

    const std::string above = replaced(document.source(), "// F：", "log.info(\"start\")\n// F：");
    const Document shifted(above, environment(), sync.layout(), &sync);
    CHECK((*nodeTitled(shifted.graph(""), "点某处"))["breakpoint"] == "off");
    CHECK(shifted.breakpointPositions()[0]["position"] == "22:11");
    const Outcome removed = shifted.toggleBreakpoint(touchAt);
    CHECK(removed.layout.breakpoints.size() == 1);
}

TEST_CASE("BP-012 comment boxes, reroutes, views and arrange live only in the layout")
{
    const Document document = c4();
    const Json graph = document.graph("");
    const std::string tap = keyOf(nodeTitled(graph, "按键 F", "单击"));
    const std::string pick = keyOf(nodeTitled(graph, "拾取 · 点一下"));
    const Outcome boxed = document.addComment("", {tap, pick});
    REQUIRE(boxed.ok());
    CHECK(boxed.key == "c1");
    CHECK(boxed.changes.empty());
    const Outcome titled = Document(document.source(), environment(), boxed.layout)
                               .editComment("", "c1", {{"title", "F：单击拾取"}, {"color", "green"}, {"width", 500}});
    const Document withBox(document.source(), environment(), titled.layout);
    const Json boxes = withBox.graph("")["comments"];
    REQUIRE(boxes.size() == 1);
    CHECK(boxes[0]["title"] == "F：单击拾取");
    CHECK(boxes[0]["color"] == "green");
    CHECK(boxes[0]["width"] == 500);
    const Json details = withBox.details("", "c1");
    CHECK(details["groups"][0]["fields"][0]["name"] == "title");

    std::string wire;
    for (const Json& item : graph["wires"]) {
        if (item["to"] == pick) {
            wire = item["key"].get<std::string>();
        }
    }
    REQUIRE(!wire.empty());
    const Outcome rerouted = withBox.addReroute("", wire, 240, 120);
    const Document withReroute(document.source(), environment(), rerouted.layout);
    const Json split = withReroute.graph("");
    const Json* dot = nullptr;
    for (const Json& node : split["nodes"]) {
        if (node["kind"] == "reroute") {
            dot = &node;
        }
    }
    REQUIRE(dot != nullptr);
    CHECK((*dot)["x"] == 240);
    CHECK(split["wires"].size() == graph["wires"].size() + 1);

    const Outcome viewed = withReroute.setView("", 12, 34, 1.25);
    CHECK(viewed.layout.pages.at("").view == ViewState{12, 34, 1.25});
    const Outcome far = withReroute.move("", {pick}, 900, 900);
    const Outcome arranged = Document(document.source(), environment(), far.layout).arrange("");
    const Document tidy(document.source(), environment(), arranged.layout);
    const std::array<double, 2> pickAt = positionOf(tidy, pick);
    const std::array<double, 2> tapAt = positionOf(tidy, tap);
    CHECK(pickAt[1] == tapAt[1]);
    CHECK(pickAt[0] == tapAt[0] + 176 + 60);
}

TEST_CASE("ADR-0091 00:5x a component's own file is its page, and component events written in logic.ts stay on the "
          "main page")
{
    Environment env = environment();
    env.controls[4].key = "F";
    env.controls[5].key = "WASD";
    const std::string source = "import { keys } from 'gamescrcpy'\n"
                               "log.info(\"start\")\n"
                               "\n"
                               "controls.开火.onPress(async () => {\n"
                               "  await sleep(100)\n"
                               "})\n"
                               "controls.开火.onRelease(() => {})\n"
                               "controls.移动.onPress(() => {})\n"
                               "controls.没有.onPress(() => {})\n"
                               "\n"
                               "keys.F.onTap(() => {})\n"
                               "\n"
                               "function helper() {}\n";
    const Document document(source, env, {});
    REQUIRE(document.ok());
    const Json pages = document.pages();
    REQUIRE(pages.size() == 2);
    CHECK(pages[0]["key"] == "");
    CHECK(pageListed(pages, "fn:helper"));
    CHECK_FALSE(pageListed(pages, "control:开火"));

    const Json home = document.graph("");
    CHECK(nodeStartingWith(home, "e:controls.开火.onPress") != nullptr);
    CHECK(nodeStartingWith(home, "e:controls.开火.onRelease") != nullptr);
    CHECK(nodeStartingWith(home, "e:controls.移动.onPress") != nullptr);
    CHECK(nodeStartingWith(home, "e:controls.没有.onPress") != nullptr);
    CHECK(nodeStartingWith(home, "e:keys.F.onTap") != nullptr);
    CHECK(nodeStartingWith(home, "e:load") != nullptr);
    bool everywhere = false;
    for (const Json& item : document.catalog("", "", "", "")) {
        everywhere = everywhere || item["id"] == "event:controls.拾取.onPress";
    }
    CHECK(everywhere);

    Environment own = env;
    own.fileName = "controls/开火.ts";
    own.ownControl = "开火";
    own.files = {"controls/开火.ts", "controls/gone.ts", "helpers.ts", "logic.ts"};
    const Document file("controls.开火.onPress(async () => {\n  await sleep(100)\n})\n", own, {});
    REQUIRE(file.ok());
    const Json filePages = file.pages();
    REQUIRE(filePages.size() == 1);
    CHECK(filePages[0]["key"] == "");
    CHECK(filePages[0]["title"] == "开火 · F");
    const Json fireGraph = file.graph("");
    CHECK(fireGraph["title"] == "开火 · F");
    CHECK(nodeStartingWith(fireGraph, "e:controls.开火.onPress") != nullptr);

    bool ownEvent = false;
    bool ownMember = false;
    bool otherControl = false;
    bool ordinary = false;
    for (const Json& item : file.catalog("", "", "", "")) {
        const std::string id = item["id"].get<std::string>();
        ownEvent = ownEvent || id == "event:controls.开火.onRelease";
        ownMember = ownMember || id == "call:controls.开火.press";
        otherControl = otherControl || id.starts_with("event:controls.拾取.") || id.starts_with("call:controls.移动.");
        ordinary = ordinary || id == "call:touch.tap" || id == "flow:if" || id == "call:sleep";
    }
    CHECK(ownEvent);
    CHECK(ownMember);
    CHECK(ordinary);
    CHECK_FALSE(otherControl);

    const Json fileSidebar = file.sidebar();
    const Json* listedFiles = nullptr;
    for (const Json& section : fileSidebar) {
        if (section["key"] == "pages") {
            listedFiles = &section["items"];
        }
    }
    REQUIRE(listedFiles != nullptr);
    REQUIRE(listedFiles->size() == 4);
    CHECK((*listedFiles)[0]["key"] == "file:logic.ts");
    CHECK((*listedFiles)[0]["title"] == "主页");
    CHECK((*listedFiles)[1]["key"] == "file:controls/开火.ts");
    CHECK((*listedFiles)[1]["title"] == "开火 · F");
    CHECK((*listedFiles)[1]["icon"] != "code-xml");
    CHECK((*listedFiles)[2]["title"] == "gone");
    CHECK((*listedFiles)[3]["title"] == "helpers");
}

TEST_CASE("BP-013 the graph's shape follows the contract: every field with its type")
{
    const Document document = c4();
    const Json graph = document.graph("");
    for (const char* field : {"key", "kind", "title", "problem"}) {
        CHECK(graph[field].is_string());
    }
    CHECK(graph["readOnly"].is_boolean());
    CHECK(graph["problemLine"].is_number());
    for (const Json& node : graph["nodes"]) {
        for (const char* field : {"key", "kind", "tone", "icon", "title", "badge", "subtitle", "note", "code", "breakpoint",
                                  "breakpointReason", "problem"}) {
            CHECK(node[field].is_string());
        }
        for (const char* field : {"x", "y", "activeSince"}) {
            CHECK(node[field].is_number());
        }
        for (const char* field : {"floating", "start", "active", "visited"}) {
            CHECK(node[field].is_boolean());
        }
        for (const char* side : {"inputs", "outputs"}) {
            for (const Json& pin : node[side]) {
                for (const char* field : {"name", "label", "type", "value", "editor", "preview"}) {
                    CHECK(pin[field].is_string());
                }
                CHECK(pin["connected"].is_boolean());
                CHECK(pin["optional"].is_boolean());
                CHECK(pin["choices"].is_array());
                CHECK(pin.contains("raw"));
            }
        }
    }
    for (const Json& wire : graph["wires"]) {
        for (const char* field : {"key", "from", "fromPin", "to", "toPin", "type", "label"}) {
            CHECK(wire[field].is_string());
        }
        CHECK(wire["active"].is_boolean());
        CHECK(wire["dashed"].is_boolean());
    }
    const Json crumbs = document.crumbs("");
    REQUIRE(crumbs.size() == 1);
    CHECK(crumbs[0]["title"] == "主页");
    const Json sidebar = document.sidebar();
    REQUIRE(sidebar.size() == 9);
    CHECK(sidebar[0]["key"] == "controls");
    const Json& open = sidebar[0]["items"][2];
    CHECK(open["title"] == "开镜");
    bool state = false;
    for (const Json& member : open["members"]) {
        if (member["key"] == "holding") {
            state = member["kind"] == "state";
        }
    }
    CHECK(state);
}

TEST_CASE("BP-014 parse plus graph of a generated 2000-line file takes under 100 ms")
{
    const auto c4Block = [](int block) {
        const std::string n = std::to_string(block);
        return "// 第 " + n + " 组：单击拾取，长按开背包\n"
             + "keys.F.onTap(() => controls.拾取.tap())\n"
             + "keys.F.onLongPress(500, () => controls.背包.tap())\n"
             + "\n"
             + "// 开镜时左键连发\n"
             + "keys.MouseLeft.onPress(() => {\n"
             + "  if (controls.开镜.holding) controls.连发.start()\n"
             + "  else controls.开火.press()\n"
             + "})\n"
             + "keys.MouseLeft.onRelease(() => {\n"
             + "  controls.连发.stop()\n"
             + "  controls.开火.release()\n"
             + "})\n"
             + "\n"
             + "// 看到“拾取”就点\n"
             + "every(500, async () => {\n"
             + "  const 位置" + n + " = await vision.find(res.拾取)\n"
             + "  if (位置" + n + ") await touch.tap(位置" + n + ")\n"
             + "})\n"
             + "\n";
    };
    const auto denseBlock = [](int block) {
        const std::string n = std::to_string(block);
        return "// block " + n + "\n"
             + "keys.F.onTap(async () => {\n"
             + "  const 位置" + n + " = await vision.find(res.拾取)\n"
             + "  if (位置" + n + ") await touch.tap(位置" + n + ")\n"
             + "  else controls.开火.press()\n"
             + "  for (let i = 0; i < " + n + "; i++) {\n"
             + "    await sleep(10 + i)\n"
             + "    log.info(\"tick\", i, controls.开镜.holding && !vars.在车上.value)\n"
             + "  }\n"
             + "})\n";
    };
    const auto generate = [](const auto& block) {
        std::string source;
        int lines = 0;
        for (int n = 0; lines < 2000; ++n) {
            const std::string text = block(n);
            lines += static_cast<int>(std::ranges::count(text, '\n'));
            source += text;
        }
        return source;
    };
    const gs::testing::ThreadCpuTime clock;
    REQUIRE(clock.usable());
    const auto fastest = [&clock](const std::string& source, std::size_t& nodes, std::size_t& wires) {
        double best = 1e9;
        for (int run = 0; run < 5; ++run) {
            const std::uint64_t start = clock.now();
            const Document document(source, environment(), {});
            const Graph view = document.view("");
            const std::uint64_t end = clock.now();
            REQUIRE(document.ok());
            nodes = view.nodes.size();
            wires = view.wires.size();
            best = std::min(best, clock.micros(start, end) / 1000.0);
        }
        return best;
    };
    const std::string typical = generate(c4Block);
    const std::string dense = generate(denseBlock);
    std::size_t typicalNodes = 0;
    std::size_t typicalWires = 0;
    std::size_t denseNodes = 0;
    std::size_t denseWires = 0;
    const double typicalMs = fastest(typical, typicalNodes, typicalWires);
    const double denseMs = fastest(dense, denseNodes, denseWires);
    MESSAGE("2000 lines like the C4 example, thread CPU time: " << typicalMs << " ms for " << typicalNodes
                                                                << " nodes and " << typicalWires
                                                                << " wires; 2000 dense lines: " << denseMs
                                                                << " ms for " << denseNodes << " nodes and "
                                                                << denseWires << " wires");
    CHECK(typicalNodes > 1000);
    CHECK(denseNodes > 2000);
#if !defined(__SANITIZE_ADDRESS__)
    CHECK(typicalMs < 100.0);
#endif
}

TEST_CASE("BP-030 a parallel of arrows draws the branches as wires from the exec output before it (issue 16)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await touch.tap(0.5, 0.5)\n"
                               "  await parallel(async () => {\n"
                               "    await sleep(100)\n"
                               "  }, async () => {\n"
                               "    await controls.开火.press()\n"
                               "  })\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    CHECK(nodeTitled(graph, "parallel") == nullptr);
    const Json* tap = nodeTitled(graph, "点某处");
    const Json* wait = nodeTitled(graph, "等待");
    const Json* fire = nodeTitled(graph, "开火 · 按下");
    REQUIRE(tap != nullptr);
    REQUIRE(wait != nullptr);
    REQUIRE(fire != nullptr);
    CHECK(wired(graph, tap, "exec", wait, "exec"));
    CHECK(wired(graph, tap, "exec", fire, "exec"));
    CHECK(graph["wires"].size() == 3);
    CHECK((*pinNamed(*tap, "outputs", "exec"))["connected"] == true);
    const std::array<double, 2> tapAt = positionOf(document, keyOf(tap));
    const std::array<double, 2> waitAt = positionOf(document, keyOf(wait));
    const std::array<double, 2> fireAt = positionOf(document, keyOf(fire));
    CHECK(waitAt[0] == tapAt[0] + 176 + 60);
    CHECK(fireAt[0] == waitAt[0]);
    CHECK(waitAt[1] < fireAt[1]);
}

TEST_CASE("BP-030 a parallel at the start of an event and inside a branch or a loop hangs from that pin (issue 16)")
{
    const std::string first = "keys.G.onPress(async () => {\n"
                              "  await parallel(async () => {\n"
                              "    await sleep(100)\n"
                              "  }, async () => {\n"
                              "    await controls.开火.press()\n"
                              "  })\n"
                              "})\n";
    const Document starting(first, environment(), {});
    REQUIRE(starting.ok());
    const Json startGraph = starting.graph("");
    const Json* startEvent = nodeTitled(startGraph, "按键 G");
    const Json* startWait = nodeTitled(startGraph, "等待");
    const Json* startFire = nodeTitled(startGraph, "开火 · 按下");
    REQUIRE(startEvent != nullptr);
    REQUIRE(startWait != nullptr);
    REQUIRE(startFire != nullptr);
    CHECK(wired(startGraph, startEvent, "exec", startWait, "exec"));
    CHECK(wired(startGraph, startEvent, "exec", startFire, "exec"));
    CHECK(startGraph["wires"].size() == 2);

    const std::string branch = "keys.F.onPress(async () => {\n"
                               "  if (controls.开镜.holding) {\n"
                               "    await parallel(async () => {\n"
                               "      await sleep(100)\n"
                               "    }, async () => {\n"
                               "      await controls.开火.press()\n"
                               "    })\n"
                               "  }\n"
                               "})\n";
    const Document branched(branch, environment(), {});
    REQUIRE(branched.ok());
    const Json branchGraph = branched.graph("");
    CHECK(nodeTitled(branchGraph, "parallel") == nullptr);
    const Json* choose = nodeTitled(branchGraph, "判断");
    const Json* trueWait = nodeTitled(branchGraph, "等待");
    const Json* trueFire = nodeTitled(branchGraph, "开火 · 按下");
    REQUIRE(choose != nullptr);
    REQUIRE(trueWait != nullptr);
    REQUIRE(trueFire != nullptr);
    CHECK(wired(branchGraph, choose, "true", trueWait, "exec"));
    CHECK(wired(branchGraph, choose, "true", trueFire, "exec"));

    const std::string loop = "keys.F.onPress(async () => {\n"
                             "  for (let i = 0; i < 2; i++) {\n"
                             "    await parallel(async () => {\n"
                             "      await sleep(100)\n"
                             "    }, async () => {\n"
                             "      await controls.开火.press()\n"
                             "    })\n"
                             "  }\n"
                             "})\n";
    const Document looping(loop, environment(), {});
    REQUIRE(looping.ok());
    const Json loopGraph = looping.graph("");
    CHECK(nodeTitled(loopGraph, "parallel") == nullptr);
    const Json* repeat = nodeTitled(loopGraph, "重复");
    const Json* eachWait = nodeTitled(loopGraph, "等待");
    const Json* eachFire = nodeTitled(loopGraph, "开火 · 按下");
    REQUIRE(repeat != nullptr);
    REQUIRE(eachWait != nullptr);
    REQUIRE(eachFire != nullptr);
    CHECK(wired(loopGraph, repeat, "each", eachWait, "exec"));
    CHECK(wired(loopGraph, repeat, "each", eachFire, "exec"));
}

TEST_CASE("BP-030 a parallel that is not several plain arrows stays the flow block it is today (issue 16)")
{
    const std::string plain = "keys.F.onPress(async () => {\n"
                              "  await parallel(sleep(100), controls.开火.press())\n"
                              "})\n";
    const Document values(plain, environment(), {});
    REQUIRE(values.ok());
    const Json graph = values.graph("");
    const Json* block = nodeTitled(graph, "parallel");
    REQUIRE(block != nullptr);
    const Json* event = nodeTitled(graph, "按键 F");
    REQUIRE(event != nullptr);
    CHECK(wired(graph, event, "exec", block, "exec"));
    CHECK(graph["wires"].size() == 1);

    const std::string single = "keys.F.onPress(async () => {\n"
                               "  await parallel(async () => {\n"
                               "    await sleep(100)\n"
                               "  })\n"
                               "})\n";
    const Document alone(single, environment(), {});
    REQUIRE(alone.ok());
    CHECK(nodeTitled(alone.graph(""), "parallel") != nullptr);

    const std::string used = "keys.F.onPress(async () => {\n"
                             "  const work = await parallel(async () => {\n"
                             "    await sleep(100)\n"
                             "  }, async () => {\n"
                             "    await controls.开火.press()\n"
                             "  })\n"
                             "})\n";
    const Document result(used, environment(), {});
    REQUIRE(result.ok());
    const Json usedGraph = result.graph("");
    CHECK(nodeTitled(usedGraph, "parallel") != nullptr);
    CHECK(nodeTitled(usedGraph, "等待") == nullptr);

    const std::string later = "keys.F.onPress(async () => {\n"
                              "  await parallel(async () => {\n"
                              "    await sleep(100)\n"
                              "  }, async () => {\n"
                              "    await controls.开火.press()\n"
                              "  })\n"
                              "  controls.开火.release()\n"
                              "})\n";
    const Document followed(later, environment(), {});
    REQUIRE(followed.ok());
    CHECK(nodeTitled(followed.graph(""), "parallel") != nullptr);
}

TEST_CASE("BP-017 a block that uses a component the profile no longer has says so and offers the two fixes")
{
    Environment env = environment();
    env.gone = {{"连射", "连射六", "Hold"}};
    const std::string source = "keys.F.onPress(async () => {\n"
                              "  controls.连射.press();\n"
                              "  log.info(\"连射\");\n"
                              "});\n";
    const Document document(source, env, {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    const Json* marked = nullptr;
    for (const Json& node : graph["nodes"]) {
        if (node["fix"] == "missing") {
            marked = &node;
        }
    }
    REQUIRE(marked != nullptr);
    CHECK((*marked)["fixControl"] == "连射");
    CHECK((*marked)["problem"] == "找不到组件“连射六”：已被删除");

    Environment unknown = environment();
    const Document plain(source, unknown, {});
    REQUIRE(plain.ok());
    const Json plainGraph = plain.graph("");
    const Json* bare = nullptr;
    for (const Json& node : plainGraph["nodes"]) {
        if (node["fix"] == "missing") {
            bare = &node;
        }
    }
    REQUIRE(bare != nullptr);
    CHECK((*bare)["problem"] == "找不到组件“连射”：已被删除");
}

TEST_CASE("BP-018 a block that uses a deleted component can be pointed at a component that is still there")
{
    Environment env = environment();
    env.gone = {{"连射", "连射六", "Hold"}};
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  controls.连射.press();\n"
                               "  log.info(\"连射\");\n"
                               "});\n";
    const Document document(source, env, {});
    REQUIRE(document.ok());
    const Json graph = document.graph("");
    std::string key;
    for (const Json& node : graph["nodes"]) {
        if (node["fix"] == "missing") {
            key = node["key"].get<std::string>();
        }
    }
    REQUIRE(!key.empty());
    const Outcome outcome = document.replaceControl(key, "连射", "开火");
    REQUIRE(outcome.ok());
    REQUIRE(outcome.changes.size() == 1);
    CHECK(applied(source, outcome.changes) == "keys.F.onPress(async () => {\n"
                                               "  controls.开火.press();\n"
                                               "  log.info(\"连射\");\n"
                                               "});\n");
    const Outcome missing = document.replaceControl(key, "不在", "开火");
    CHECK_FALSE(missing.ok());
}

TEST_CASE("BP-078 variables are listed with their type, category and description; locals only on their page (issue 31)")
{
    const std::string source = "/** 英雄的血量 @category 战斗 */\n"
                               "let 血量: number = 100\n"
                               "/** @category 战斗 */\n"
                               "let 目标: PicturePoint[] = []\n"
                               "keys.F.onPress(async () => {})\n";
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const auto sectionOf = [](const Json& sidebar, std::string_view key) -> const Json* {
        for (const Json& section : sidebar) {
            if (section["key"].get<std::string>() == key) {
                return &section;
            }
        }
        return nullptr;
    };
    const auto hasItem = [](const Json& sidebar, std::string_view key) {
        for (const Json& section : sidebar) {
            for (const Json& item : section["items"]) {
                if (item["key"].get<std::string>() == key) {
                    return true;
                }
            }
        }
        return false;
    };
    const Json sidebar = document.sidebar();
    const Json* vars = sectionOf(sidebar, "vars");
    REQUIRE(vars != nullptr);
    CHECK((*vars)["addable"] == true);
    const Json& items = (*vars)["items"];
    REQUIRE(items.size() >= 2);
    CHECK(items[0]["key"] == "variable:血量");
    CHECK(items[0]["title"] == "血量");
    CHECK(items[0]["badge"] == "整数");
    CHECK(items[0]["category"] == "战斗");
    CHECK(items[0]["description"] == "英雄的血量");
    CHECK(items[0]["dot"] == "variable");
    CHECK(items[1]["key"] == "variable:目标");
    CHECK(items[1]["badge"] == "位置列表");
    CHECK(items[1]["category"] == "战斗");

    const std::string withLocal = "async function attack() {\n  let 次数 = 0\n  await sleep(100)\n}\n";
    const Document local(withLocal, environment(), {});
    CHECK_FALSE(hasItem(local.sidebar(), "local:次数"));
    CHECK(hasItem(local.sidebar("fn:attack"), "local:次数"));

    const Json graph = document.graph("");
    const Json* health = nodeTitled(graph, "设置 血量");
    REQUIRE(health != nullptr);
    CHECK((*health)["note"] == "英雄的血量");
    const Json* target = nodeTitled(graph, "设置 目标");
    REQUIRE(target != nullptr);
    CHECK(target->value("note", std::string()).empty());
}
