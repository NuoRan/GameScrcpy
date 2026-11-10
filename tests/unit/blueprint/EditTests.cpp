#include "Fixture.h"

#include <gs/blueprint/Text.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace gs::blueprint;
using namespace blueprint_test;

namespace {

const GraphNode* titled(const Graph& graph, std::string_view title)
{
    for (const GraphNode& node : graph.nodes) {
        if (node.title == title) {
            return &node;
        }
    }
    return nullptr;
}

std::string key(const Graph& graph, std::string_view title)
{
    const GraphNode* node = titled(graph, title);
    REQUIRE(node != nullptr);
    return std::string(node->key);
}

std::string after(const Document& document, const Outcome& outcome)
{
    REQUIRE_MESSAGE(outcome.ok(), outcome.reason);
    return applied(document.source(), outcome.changes);
}

}

TEST_CASE("BP-020 inserting into an expression arrow makes a block with await and async")
{
    const std::string source = "keys.F.onTap(() => controls.拾取.tap())\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string tap = key(graph, "拾取 · 点一下");
    const Outcome outcome = document.add("", "call:touch.tap", 400, 40, tap, "exec");
    CHECK(after(document, outcome)
          == "keys.F.onTap(async () => {\n  await controls.拾取.tap()\n  await touch.tap(0.5, 0.5)\n})\n");
    CHECK_FALSE(outcome.key.empty());
}

TEST_CASE("BP-021 values are written as literals and nothing else changes")
{
    const std::string source = "// 点一下\r\nkeys.F.onPress(async () => {\r\n  await touch.tap(0.5, 0.5) // 中间\r\n"
                               "  await sleep(500)\r\n})\r\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string tap = key(graph, "点某处");
    const std::string wait = key(graph, "等待");
    CHECK(after(document, document.setValue(tap, "x", nlohmann::json{{"x", 0.2}, {"y", 0.25}}))
          == "// 点一下\r\nkeys.F.onPress(async () => {\r\n  await touch.tap(0.2, 0.25) // 中间\r\n"
             "  await sleep(500)\r\n})\r\n");
    CHECK(after(document, document.setValue(wait, "ms", 800))
          == "// 点一下\r\nkeys.F.onPress(async () => {\r\n  await touch.tap(0.5, 0.5) // 中间\r\n"
             "  await sleep(800)\r\n})\r\n");
}

TEST_CASE("BP-021 a point picked on the picture is written as a named point of the profile")
{
    const Document plain("keys.F.onPress(async () => {\n  await touch.tap(0.5, 0.5)\n})\n", environment(), {});
    CHECK(after(plain, plain.setValue(key(plain.view(""), "点某处"), "x", "入口"))
          == "keys.F.onPress(async () => {\n  await touch.tap(res.point(\"入口\"))\n})\n");
    const Document timed("keys.F.onPress(async () => {\n  await touch.tap(0.5, 0.5, 100)\n})\n", environment(), {});
    CHECK(after(timed, timed.setValue(key(timed.view(""), "点某处"), "x", "入口"))
          == "keys.F.onPress(async () => {\n  await touch.tap(res.point(\"入口\"), undefined, 100)\n})\n");
}

TEST_CASE("BP-022 event details rename the key, switch the method and write the repeat rule")
{
    const std::string source = "keys.F.onTap(() => controls.拾取.tap())\n";
    const Document document(source, environment(), {});
    const std::string event = key(document.view(""), "按键 F");
    CHECK(after(document, document.editDetail("", event, "key", "G")) == "keys.G.onTap(() => controls.拾取.tap())\n");
    CHECK(after(document, document.editDetail("", event, "when", "onLongPress"))
          == "keys.F.onLongPress(500, () => controls.拾取.tap())\n");
    CHECK(after(document, document.editDetail("", event, "again", "queue"))
          == "keys.F.onTap(() => controls.拾取.tap(), { again: \"queue\" })\n");

    const std::string held = "keys.F.onLongPress(500, () => controls.拾取.tap(), { again: \"queue\" })\n";
    const Document pressing(held, environment(), {});
    const std::string longEvent = key(pressing.view(""), "按键 F");
    CHECK(after(pressing, pressing.editDetail("", longEvent, "holdMs", 800))
          == "keys.F.onLongPress(800, () => controls.拾取.tap(), { again: \"queue\" })\n");
    CHECK(after(pressing, pressing.editDetail("", longEvent, "again", "ignore"))
          == "keys.F.onLongPress(500, () => controls.拾取.tap())\n");
    CHECK(after(pressing, pressing.editDetail("", longEvent, "when", "onTap"))
          == "keys.F.onTap(() => controls.拾取.tap(), { again: \"queue\" })\n");
}

TEST_CASE("BP-022 a component's key event shows that it takes the key, and can let the component press first "
          "(issue 34)")
{
    FakeLabels named = labels();
    named.chinese["badge:logicOnly"] = "只运行逻辑";
    named.chinese["badge:alsoPresses"] = "先照常按下";
    Environment env = environment();
    env.labels = &named;
    const std::string rest = "controls.移动.onPress(() => {})\n";
    const std::string source = "controls.开火.onPress(async () => {\n  await sleep(100)\n})\n" + rest;
    const Document document(source, env, {});
    const Graph firePage = document.view("");
    const Graph movePage = document.view("");
    const auto eventOf = [](const Graph& graph, std::string_view prefix) -> const GraphNode* {
        const auto found = std::ranges::find_if(graph.nodes, [prefix](const GraphNode& node) {
            return node.key.starts_with(prefix);
        });
        return found != graph.nodes.end() ? &*found : nullptr;
    };
    const GraphNode* fire = eventOf(firePage, "e:controls.开火.onPress");
    const GraphNode* move = eventOf(movePage, "e:controls.移动.onPress");
    REQUIRE(fire != nullptr);
    REQUIRE(move != nullptr);
    CHECK(fire->badge == "只运行逻辑");
    CHECK(move->badge.empty());

    const std::string both = after(document, document.editDetail("", std::string(fire->key), "consume", "both"));
    CHECK(both == "controls.开火.onPress(async () => {\n  await sleep(100)\n}, { consume: false })\n" + rest);
    const Document pressing(both, env, {});
    const Graph pressingGraph = pressing.view("");
    const GraphNode* first = eventOf(pressingGraph, "e:controls.开火.onPress");
    REQUIRE(first != nullptr);
    CHECK(first->badge == "先照常按下");
    CHECK(after(pressing, pressing.editDetail("", std::string(first->key), "consume", "logic")) == source);

    const Document queued("controls.开火.onPress(() => {}, { again: \"queue\" })\n", env, {});
    const Graph queuedGraph = queued.view("");
    const GraphNode* queuedFire = eventOf(queuedGraph, "e:controls.开火.onPress");
    REQUIRE(queuedFire != nullptr);
    CHECK(after(queued, queued.editDetail("", std::string(queuedFire->key), "consume", "both"))
          == "controls.开火.onPress(() => {}, { again: \"queue\", consume: false })\n");
}

TEST_CASE("BP-023 refusals carry their reasons")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  controls.开火.press()\n"
                               "})\n"
                               "keys.G.onPress(async () => {\n"
                               "  const 位置 = await vision.find(res.拾取)\n"
                               "  await sleep(100)\n"
                               "})\n";
    FakeLabels named = labels();
    named.chinese["reason:secondExec"] = "一个方块只能接在一处后面；要在几处都做，把它折叠成函数再分别调用";
    named.chinese["reason:type"] = "不能把“%1”接到“%2”上";
    named.chinese["reason:contiguous"] = "只能折叠一段连着的方块";
    Environment env = environment();
    env.labels = &named;
    const Document document(source, env, {});
    const Graph graph = document.view("");
    const std::string pick = key(graph, "拾取 · 点一下");
    const std::string fire = key(graph, "开火 · 按下");
    const std::string g = key(graph, "按键 G");
    const std::string wait = key(graph, "等待");
    CHECK(document.check(g, "exec", fire, "exec")
          == "一个方块只能接在一处后面；要在几处都做，把它折叠成函数再分别调用");
    CHECK_FALSE(document.connect(g, "exec", fire, "exec").ok());
    const GraphNode* find = titled(graph, "找图");
    REQUIRE(find != nullptr);
    CHECK(document.check(std::string(find->key), "位置", wait, "ms").find("接到") != std::string::npos);
    CHECK(document.collapse({pick, wait}).reason == "只能折叠一段连着的方块");
}

TEST_CASE("BP-021 a region picked on the picture fills the search area of a find, with or without options (issue 23)")
{
    const std::string source = "keys.G.onPress(async () => {\n"
                               "  const 位置 = await vision.find(res.拾取)\n"
                               "  if (位置) {\n"
                               "    await touch.tap(位置)\n"
                               "  }\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const GraphNode* find = titled(graph, "找图");
    REQUIRE(find != nullptr);
    const std::string written = after(document, document.setValue(std::string(find->key), "options.region", "区域 1"));
    CHECK(written.find("vision.find(res.拾取, { region: ") != std::string::npos);
    CHECK(written.find("区域 1") != std::string::npos);

    const Document again(written, environment(), {});
    const Graph second = again.view("");
    const GraphNode* findAgain = titled(second, "找图");
    REQUIRE(findAgain != nullptr);
    const std::string replaced = after(again, again.setValue(std::string(findAgain->key), "options.region", "区域 2"));
    CHECK(replaced.find("区域 2") != std::string::npos);
    CHECK(replaced.find("区域 1") == std::string::npos);
}

TEST_CASE("BP-021 region and point pins offer the profile's regions and points to choose from (issue 23)")
{
    Environment env = environment();
    env.regions = {"区域 1", "区域 2"};
    env.points = {"入口"};
    const Document document("keys.G.onPress(async () => {\n"
                            "  await vision.find(res.拾取, { region: \"区域 2\" })\n"
                            "  await touch.tap(0.5, 0.5)\n"
                            "})\n",
                            env, {});
    const Graph graph = document.view("");
    const auto pinNamed = [](const GraphNode& node, std::string_view name) -> const GraphPin* {
        const auto found = std::ranges::find(node.inputs, name, &GraphPin::name);
        return found != node.inputs.end() ? &*found : nullptr;
    };
    const GraphNode* find = titled(graph, "找图");
    REQUIRE(find != nullptr);
    const GraphPin* region = pinNamed(*find, "options.region");
    REQUIRE(region != nullptr);
    REQUIRE(region->choices.size() == 2);
    CHECK(region->choices[1].value == "区域 2");
    const GraphNode* tap = titled(graph, "点某处");
    REQUIRE(tap != nullptr);
    const GraphPin* at = pinNamed(*tap, "x");
    REQUIRE(at != nullptr);
    REQUIRE(at->choices.size() == 1);
    CHECK(at->choices[0].value == "入口");
    CHECK(after(document, document.setValue(std::string(tap->key), "x", "入口"))
              .find("touch.tap(res.point(\"入口\"))") != std::string::npos);
}

TEST_CASE("BP-024 disconnecting floats the rest and reconnecting restores the code")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  await sleep(100)\n"
                               "  controls.开火.press()\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string pick = key(graph, "拾取 · 点一下");
    const Outcome cut = document.disconnect(pick, "exec");
    const std::string cutText = after(document, cut);
    CHECK(cutText == "keys.F.onPress(async () => {\n  await controls.拾取.tap()\n})\n");
    const Document floating(cutText, environment(), cut.layout, &document);
    const Graph floated = floating.view("");
    const GraphNode* wait = titled(floated, "等待");
    REQUIRE(wait != nullptr);
    CHECK(wait->floating);
    const GraphNode* fire = titled(floated, "开火 · 按下");
    REQUIRE(fire != nullptr);
    CHECK(fire->floating);
    const std::string newPick = key(floated, "拾取 · 点一下");
    const Outcome joined = floating.connect(newPick, "exec", std::string(wait->key), "exec");
    CHECK(after(floating, joined) == source);
    const Document back(after(floating, joined), environment(), joined.layout, &floating);
    const Graph backGraph = back.view("");
    const GraphNode* restored = titled(backGraph, "等待");
    REQUIRE(restored != nullptr);
    CHECK_FALSE(restored->floating);
    CHECK(back.layout().pages.at("").floating.empty());
}

TEST_CASE("BP-025 removing, copying and pasting statements")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  // 等一下\n"
                               "  await sleep(100)\n"
                               "  controls.开火.press()\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string wait = key(graph, "等待");
    const std::string fire = key(graph, "开火 · 按下");
    CHECK(after(document, document.remove("", {wait}))
          == "keys.F.onPress(async () => {\n  await controls.拾取.tap()\n  controls.开火.press()\n})\n");
    CHECK(document.copy({wait, fire}) == "// 等一下\nawait sleep(100)\ncontrols.开火.press()");
    const std::string event = key(graph, "按键 F");
    CHECK(document.copy({event}) + "\n" == source);
    const Outcome pasted = document.paste("", "keys.G.onTap(() => controls.背包.tap())\n", 40, 400);
    CHECK(after(document, pasted) == source + "keys.G.onTap(() => controls.背包.tap())\n");
    CHECK_FALSE(document.paste("", "keys.G.onTap(() => {", 0, 0).ok());
}

TEST_CASE("BP-025 removing a branch in the middle keeps the blocks inside it on the canvas")
{
    const std::string source = "let 运行中 = true\n"
                               "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  if (运行中) {\n"
                               "    await sleep(200)\n"
                               "    await touch.tap(0.5, 0.5)\n"
                               "  }\n"
                               "  controls.开火.press()\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Outcome removed = document.remove("", {key(document.view(""), "判断")});
    const Document next(after(document, removed), environment(), removed.layout);
    const Graph graph = next.view("");
    CHECK(titled(graph, "等待") != nullptr);
    CHECK(titled(graph, "点某处") != nullptr);
    CHECK(titled(graph, "开火 · 按下") != nullptr);

    const std::string leaving = "let 运行中 = true\n"
                                "keys.F.onPress(async () => {\n"
                                "  if (运行中) {\n"
                                "    运行中 = false\n"
                                "    return\n"
                                "  }\n"
                                "  controls.开火.press()\n"
                                "})\n";
    const Document early(leaving, environment(), {});
    const Outcome cut = early.remove("", {key(early.view(""), "判断")});
    const Document rest(after(early, cut), environment(), cut.layout);
    const Graph left = rest.view("");
    CHECK(titled(left, "设置 运行中") != nullptr);
    CHECK(titled(left, "返回") != nullptr);
    CHECK(titled(left, "开火 · 按下") != nullptr);
}

TEST_CASE("BP-025 deleting a branch in the middle puts its true branch in its place and floats the false branch "
          "(issue 33f)")
{
    const std::string source = "let 运行中 = true\n"
                               "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  if (运行中) {\n"
                               "    await sleep(200)\n"
                               "    await touch.tap(0.5, 0.5)\n"
                               "  } else {\n"
                               "    controls.开火.press()\n"
                               "  }\n"
                               "  await controls.背包.tap()\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Outcome removed = document.remove("", {key(document.view(""), "判断")});
    CHECK(after(document, removed) == "let 运行中 = true\n"
                                      "keys.F.onPress(async () => {\n"
                                      "  await controls.拾取.tap()\n"
                                      "  await sleep(200)\n"
                                      "  await touch.tap(0.5, 0.5)\n"
                                      "  await controls.背包.tap()\n"
                                      "})\n");
    const std::vector<FloatingCode>& floating = removed.layout.pages.at("").floating;
    REQUIRE(floating.size() == 1);
    CHECK(floating.front().code == "controls.开火.press()");
    const Document next(after(document, removed), environment(), removed.layout);
    const Graph graph = next.view("");
    const GraphNode* wait = titled(graph, "等待");
    REQUIRE(wait != nullptr);
    CHECK_FALSE(wait->floating);
    const GraphNode* fire = titled(graph, "开火 · 按下");
    REQUIRE(fire != nullptr);
    CHECK(fire->floating);
}

TEST_CASE("BP-025 deleting a find in the middle keeps what it did when found and clears the position it gave "
          "(issue 33f)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  const 位置 = await vision.find(res.拾取)\n"
                               "  if (位置) {\n"
                               "    await touch.tap(位置)\n"
                               "  }\n"
                               "  await sleep(100)\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Outcome removed = document.remove("", {key(document.view(""), "找图")});
    CHECK(after(document, removed) == "keys.F.onPress(async () => {\n  await touch.tap(undefined)\n  await sleep(100)\n})\n");
    const Document next(after(document, removed), environment(), removed.layout);
    const Graph graph = next.view("");
    const GraphNode* tap = titled(graph, "点某处");
    REQUIRE(tap != nullptr);
    CHECK_FALSE(tap->floating);
    CHECK(tap->problem == "“位置”没有值：接一根线或填一个值");
    const GraphNode* wait = titled(graph, "等待");
    REQUIRE(wait != nullptr);
    CHECK(wait->problem.empty());
}

TEST_CASE("BP-025 deleting a loop keeps its body unless the body leaves the loop (issue 33f)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  for (let i = 0; i < 3; i++) {\n"
                               "    if (i > 1) break\n"
                               "    await touch.tap(0.5, 0.5)\n"
                               "  }\n"
                               "  for (const name of [\"a\", \"b\"]) log.info(name)\n"
                               "  await sleep(100)\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const Outcome each = document.remove("", {key(graph, "对每个")});
    CHECK(after(document, each) == "keys.F.onPress(async () => {\n"
                                   "  for (let i = 0; i < 3; i++) {\n"
                                   "    if (i > 1) break\n"
                                   "    await touch.tap(0.5, 0.5)\n"
                                   "  }\n"
                                   "  log.info(undefined)\n"
                                   "  await sleep(100)\n"
                                   "})\n");
    const auto eachPage = each.layout.pages.find("");
    CHECK((eachPage == each.layout.pages.end() || eachPage->second.floating.empty()));
    const Outcome repeat = document.remove("", {key(graph, "重复")});
    CHECK(after(document, repeat) == "keys.F.onPress(async () => {\n"
                                     "  for (const name of [\"a\", \"b\"]) log.info(name)\n"
                                     "  await sleep(100)\n"
                                     "})\n");
    const std::vector<FloatingCode>& floating = repeat.layout.pages.at("").floating;
    REQUIRE(floating.size() == 1);
    CHECK(floating.front().code == "if (i > 1) break\nawait touch.tap(0.5, 0.5)");
}

TEST_CASE("BP-026 code blocks are edited in place and must parse")
{
    const std::string source = "keys.F.onPress(() => {\n  let n = 0\n  n++\n  controls.开火.press()\n})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string block = key(graph, "代码块");
    CHECK(after(document, document.setCode(block, "n += 2"))
          == "keys.F.onPress(() => {\n  let n = 0\n  n += 2\n  controls.开火.press()\n})\n");
    CHECK_FALSE(document.setCode(block, "n +=").ok());
}

TEST_CASE("issue 31: adding the flow blocks writes their code and reading it back gives the same block")
{
    const std::string source = "keys.F.onPress(async () => {\n  await sleep(100)\n})\n";
    const Document document(source, environment(), {});
    const std::string wait = key(document.view(""), "等待");
    const Outcome added = document.add("", "flow:switch", 400, 40, wait, "exec");
    const std::string switchText = after(document, added);
    CHECK(switchText == "keys.F.onPress(async () => {\n  await sleep(100)\n  switch (0) {\n"
                        "    case 1: {}\n    case 2: {}\n    default: {}\n  }\n})\n");
    const Document again(switchText, environment(), {}, &document);
    const Graph graph = again.view("");
    CHECK(titled(graph, "代码块") == nullptr);
    const GraphNode* branch = titled(graph, "多分支");
    REQUIRE(branch != nullptr);
    REQUIRE(branch->outputs.size() == 4);
    CHECK(std::string(branch->outputs[0].name) == "case:0");
    CHECK(std::string(branch->outputs[0].label) == "1");
    CHECK(std::string(branch->outputs[2].name) == "default");
    const std::string branchKey(branch->key);
    CHECK(after(again, again.editDetail("", branchKey, "cases", "10, \"b\""))
          == "keys.F.onPress(async () => {\n  await sleep(100)\n  switch (0) {\n"
             "    case 10: {}\n    case \"b\": {}\n    default: {}\n  }\n})\n");
    CHECK_FALSE(again.editDetail("", branchKey, "cases", "1, 2, 3").ok());
    CHECK_FALSE(again.editDetail("", branchKey, "cases", "1, ").ok());

    const auto addOne = [](std::string_view id, std::string_view title, std::string_view snippet) {
        const Document start("keys.F.onPress(async () => {\n  await sleep(100)\n})\n", environment(), {});
        const std::string from = key(start.view(""), "等待");
        const Outcome outcome = start.add("", id, 400, 40, from, "exec");
        CAPTURE(id);
        const std::string text = after(start, outcome);
        CHECK(text.find(snippet) != std::string::npos);
        const Document next(text, environment(), {}, &start);
        CHECK(titled(next.view(""), title) != nullptr);
        return text;
    };
    addOne("flow:sequence", "顺序", "await sequence(async () => {");
    addOne("flow:multiGate", "多路门", "multiGate(\"门\", 3, false, false, async () => {\n  }, async () => {");
    addOne("flow:forRange", "按范围重复", "for (let i = 0; i <= 2; i++) {\n  }");
    addOne("flow:forEachIndex", "带序号的对每个", "for (const [i, item] of [].entries()) {\n  }");
}

TEST_CASE("issue 31: a list block dropped on a condition is written inline and read back as a pure block")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  const names = []\n"
                               "  if (names.length > 0) {\n"
                               "    await sleep(100)\n"
                               "  }\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const GraphNode* branch = titled(graph, "判断");
    REQUIRE(branch != nullptr);
    const std::string text =
        after(document, document.add("", "pure:list.has", 400, 40, std::string(branch->key), "condition"));
    CHECK(text == "keys.F.onPress(async () => {\n  const names = []\n  if (list.has([], undefined)) {\n"
                  "    await sleep(100)\n  }\n})\n");
    const Document again(text, environment(), {}, &document);
    const Graph againGraph = again.view("");
    const GraphNode* has = titled(againGraph, "list.has");
    REQUIRE(has != nullptr);
    CHECK(std::string(has->kind) == "pure");
    bool items = false;
    bool value = false;
    for (const GraphPin& pin : has->inputs) {
        items = items || pin.name == "items";
        value = value || pin.name == "value";
    }
    CHECK(items);
    CHECK(value);
}

TEST_CASE("issue 31: a block added to a switch case stays inside that case")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  switch (1) {\n"
                               "    case 1: {\n"
                               "    }\n"
                               "    default: {\n"
                               "      await sleep(100)\n"
                               "    }\n"
                               "  }\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const GraphNode* branch = titled(graph, "多分支");
    REQUIRE(branch != nullptr);
    const std::string branchKey(branch->key);
    CHECK(after(document, document.add("", "flow:break", 400, 40, branchKey, "case:0"))
          == "keys.F.onPress(async () => {\n  switch (1) {\n    case 1: {\n      break\n    }\n    default: {\n"
             "      await sleep(100)\n    }\n  }\n})\n");
    CHECK(after(document, document.add("", "flow:break", 400, 40, branchKey, "default"))
          == "keys.F.onPress(async () => {\n  switch (1) {\n    case 1: {\n    }\n    default: {\n"
             "      break\n      await sleep(100)\n    }\n  }\n})\n");

    const Document plain("keys.F.onPress(async () => {\n"
                         "  switch (1) {\n"
                         "    case 1:\n"
                         "    default:\n"
                         "  }\n"
                         "})\n",
                         environment(), {});
    const Graph plainGraph = plain.view("");
    const GraphNode* open = titled(plainGraph, "多分支");
    REQUIRE(open != nullptr);
    const std::string text = after(plain, plain.add("", "flow:break", 400, 40, std::string(open->key), "case:0"));
    REQUIRE(text.find("break") != std::string::npos);
    CHECK(text.find("break") < text.find("default:"));
    const Document parsed(text, environment(), {});
    CHECK_MESSAGE(parsed.ok(), parsed.problem());
    CHECK(titled(parsed.view(""), "跳出循环") != nullptr);
}

TEST_CASE("BP-027 collapsing a run of statements makes a function")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  await sleep(100)\n"
                               "  controls.开火.press()\n"
                               "})\n";
    FakeLabels named = labels();
    named.chinese["name:function"] = "新函数";
    Environment env = environment();
    env.labels = &named;
    const Document document(source, env, {});
    const Graph graph = document.view("");
    const Outcome outcome = document.collapse({key(graph, "拾取 · 点一下"), key(graph, "等待")});
    CHECK(after(document, outcome)
          == "keys.F.onPress(async () => {\n  await 新函数()\n  controls.开火.press()\n})\n\n"
             "async function 新函数() {\n  await controls.拾取.tap()\n  await sleep(100)\n}\n");
}

TEST_CASE("BP-028 data wires write a local and respect scope")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await vision.find(res.拾取)\n"
                               "  await touch.tap(0.5, 0.5)\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const GraphNode* find = titled(graph, "找图");
    REQUIRE(find != nullptr);
    REQUIRE(find->outputs.size() >= 2);
    const std::string result(find->outputs.back().name);
    const std::string tap = key(graph, "点某处");
    CHECK(document.check(std::string(find->key), result, tap, "x").empty());
    CHECK(after(document, document.connect(std::string(find->key), result, tap, "x"))
          == "keys.F.onPress(async () => {\n  const " + result + " = await vision.find(res.拾取)\n  await touch.tap("
                 + result + ")\n})\n");

    const std::string later = "keys.F.onPress(async () => {\n"
                              "  await touch.tap(0.5, 0.5)\n"
                              "  const 位置 = await vision.find(res.拾取)\n"
                              "})\n";
    FakeLabels named = labels();
    named.chinese["reason:scope"] = "“%1”在这里还没有（它在后面才算出来，或在别的分支里）";
    Environment env = environment();
    env.labels = &named;
    const Document order(later, env, {});
    const Graph orderGraph = order.view("");
    const GraphNode* found = titled(orderGraph, "找图");
    REQUIRE(found != nullptr);
    CHECK(order.check(std::string(found->key), "位置", key(orderGraph, "点某处"), "x")
          == "“位置”在这里还没有（它在后面才算出来，或在别的分支里）");
}

TEST_CASE("BP-021 a swipe keeps its duration as its ends turn into points and back (issue 12)")
{
    const auto source = [](std::string_view call) {
        return "keys.F.onPress(async () => {\n  await " + std::string(call) + "\n})\n";
    };
    const Document numbers(source("touch.swipe(0.5, 0.8, 0.5, 0.2, 250)"), environment(), {});
    const Outcome namedFrom = numbers.setValue(key(numbers.view(""), "touch.swipe"), "from", "起点");
    CHECK(after(numbers, namedFrom) == source("touch.swipe(res.point(\"起点\"), undefined, 0.5, 0.2, 250)"));

    const Document half(applied(numbers.source(), namedFrom.changes), environment(), {});
    CHECK(after(half, half.setValue(key(half.view(""), "touch.swipe"), "to", "终点"))
          == source("touch.swipe(res.point(\"起点\"), res.point(\"终点\"), 250)"));

    const Document other(source("touch.swipe(0.5, 0.8, 0.5, 0.2, 250)"), environment(), {});
    const Outcome namedTo = other.setValue(key(other.view(""), "touch.swipe"), "to", "终点");
    CHECK(after(other, namedTo) == source("touch.swipe(0.5, 0.8, res.point(\"终点\"), undefined, 250)"));

    const Document thenFrom(applied(other.source(), namedTo.changes), environment(), {});
    CHECK(after(thenFrom, thenFrom.setValue(key(thenFrom.view(""), "touch.swipe"), "from", "起点"))
          == source("touch.swipe(res.point(\"起点\"), res.point(\"终点\"), 250)"));

    const Document points(source("touch.swipe(res.point(\"起点\"), res.point(\"终点\"), 250)"), environment(), {});
    const Outcome numberedFrom =
        points.setValue(key(points.view(""), "touch.swipe"), "from", nlohmann::json{{"x", 0.5}, {"y", 0.8}});
    CHECK(after(points, numberedFrom)
          == source("touch.swipe(0.5, 0.8, res.point(\"终点\"), undefined, 250)"));

    const Document back(applied(points.source(), numberedFrom.changes), environment(), {});
    CHECK(after(back, back.setValue(key(back.view(""), "touch.swipe"), "to", nlohmann::json{{"x", 0.5}, {"y", 0.2}}))
          == source("touch.swipe(0.5, 0.8, 0.5, 0.2, 250)"));
}

TEST_CASE("BP-021 a tap with a point keeps its duration where the runtime reads it (issue 12)")
{
    const auto source = [](std::string_view call) {
        return "keys.F.onPress(async () => {\n  await " + std::string(call) + "\n})\n";
    };
    const Document late(source("touch.tap(res.point(\"入口\"), undefined, 80)"), environment(), {});
    CHECK(after(late, late.setValue(key(late.view(""), "点某处"), "x", nlohmann::json{{"x", 0.2}, {"y", 0.25}}))
          == source("touch.tap(0.2, 0.25, 80)"));

    const Document early(source("touch.tap(res.point(\"入口\"), 80)"), environment(), {});
    CHECK(after(early, early.setValue(key(early.view(""), "点某处"), "x", "出口"))
          == source("touch.tap(res.point(\"出口\"), undefined, 80)"));

    const Document plain(source("touch.tap(0.5, 0.5)"), environment(), {});
    CHECK(after(plain, plain.setValue(key(plain.view(""), "点某处"), "x", "入口"))
          == source("touch.tap(res.point(\"入口\"))"));
}

TEST_CASE("BP-024 an edit of a swipe that passes values the runtime ignores drops them (issue 12)")
{
    const std::string broken = "keys.F.onPress(async () => {\n  await touch.swipe(res.point(\"点位 10\"), "
                               "res.point(\"点位 6\"), 0.5, 0.3, 10)\n})\n";
    const Document document(broken, environment(), {});
    const Graph graph = document.view("");
    const GraphNode* swipe = titled(graph, "touch.swipe");
    REQUIRE(swipe != nullptr);
    CHECK(swipe->problem == "This call passes values that are not used");
    CHECK(after(document, document.setValue(std::string(swipe->key), "to", "终点"))
          == "keys.F.onPress(async () => {\n  await touch.swipe(res.point(\"点位 10\"), res.point(\"终点\"), 0.5)\n})\n");
}

TEST_CASE("ADR-0091 00:5x a component's own file adds its events at the top level, floats plain blocks and keeps its "
          "view")
{
    Environment env = environment();
    env.controls[4].key = "F";
    env.fileName = "controls/开火.ts";
    env.ownControl = "开火";
    const std::string source = "controls.开火.onPress(async () => {\n  await sleep(100)\n})\n";
    const Document document(source, env, {});
    REQUIRE(document.ok());
    const Graph page = document.view("");
    CHECK(page.title == "开火 · F");
    const std::size_t nodes = page.nodes.size();

    const Outcome added = document.add("", "event:controls.开火.onRelease", 240, 60, "", "");
    REQUIRE_MESSAGE(added.ok(), added.reason);
    const std::string withRelease = after(document, added);
    CHECK(withRelease == "controls.开火.onPress(async () => {\n  await sleep(100)\n})\n"
                         "controls.开火.onRelease(() => {\n})\n");
    const Document next(withRelease, env, added.layout, &document);
    CHECK(next.view("").nodes.size() == nodes + 1);
    CHECK(next.layout().pages.at("").nodes.contains(added.key));

    const Document glued("controls.开火.onPress(() => {})", env, {});
    REQUIRE(glued.ok());
    const Outcome appended = glued.add("", "event:controls.开火.onRelease", 0, 0, "", "");
    REQUIRE_MESSAGE(appended.ok(), appended.reason);
    CHECK(after(glued, appended)
          == "controls.开火.onPress(() => {})\ncontrols.开火.onRelease(() => {\n})\n");

    const Outcome floated = next.add("", "call:touch.tap", 400, 40, "", "");
    REQUIRE_MESSAGE(floated.ok(), floated.reason);
    const Document floating(after(next, floated), env, floated.layout, &next);
    bool anyFloating = false;
    for (const GraphNode& node : floating.view("").nodes) {
        anyFloating = anyFloating || node.floating;
    }
    CHECK(anyFloating);
    CHECK(floating.layout().pages.at("").floating.size() == 1);

    const Outcome viewed = floating.setView("", 12, 34, 1.25);
    CHECK(viewed.layout.pages.at("").view == ViewState{12, 34, 1.25});
    const Document reopened(after(floating, floated), env, viewed.layout, &floating);
    CHECK(reopened.view("").view == ViewState{12, 34, 1.25});
}

TEST_CASE("issue 33: the first event of a component added on the main page is found again in a file with semicolons")
{
    const std::string source = "keys.F.onPress(async () => {\n    await touch.tap(0.5, 0.5);\n});\n";
    Environment env = environment();
    env.controls[4].key = "Space";
    const Document document(source, env, {});
    const Outcome added = document.add("", "event:controls.开火.onPress", 300, 80, "", "");
    REQUIRE_MESSAGE(added.ok(), added.reason);
    const std::string text = after(document, added);
    CAPTURE(text);
    CHECK(text.find("controls.开火.onPress(") != std::string::npos);
    CHECK_FALSE(added.key.empty());
    const Document next(text, env, added.layout, &document);
    CHECK(next.layout().pages.at("").nodes.contains(added.key));
}

TEST_CASE("ADR-0091 00:5x a key event that also presses starts with a default action, and deleting it leaves the key "
          "to the logic")
{
    FakeLabels named = labels();
    named.chinese["node:defaultAction"] = "默认动作";
    Environment env = environment();
    env.labels = &named;
    env.fileName = "controls/开火.ts";
    env.ownControl = "开火";
    const std::string body = "controls.开火.onPress(() => {\n  log.info(\"after\")\n}";
    const Document document(body + ", { consume: false })\n", env, {});
    REQUIRE(document.ok());
    const Graph page = document.view("");
    const GraphNode* press = titled(page, "默认动作");
    REQUIRE(press != nullptr);
    const auto wired = [&page](std::string_view from, std::string_view to) {
        return std::ranges::any_of(page.wires, [from, to](const GraphWire& wire) {
            return wire.type == "exec" && wire.from.starts_with(from) && wire.to.starts_with(to);
        });
    };
    CHECK(wired("e:controls.开火.onPress#0", press->key));
    CHECK(wired(press->key, "e:controls.开火.onPress#0/exec"));
    CHECK_FALSE(std::ranges::any_of(page.wires, [](const GraphWire& wire) {
        return wire.from == "e:controls.开火.onPress#0" && wire.to.starts_with("e:controls.开火.onPress#0/exec");
    }));
    const auto log = std::ranges::find_if(page.nodes, [](const GraphNode& node) {
        return node.key.starts_with("e:controls.开火.onPress#0/exec");
    });
    REQUIRE(log != page.nodes.end());

    CHECK(document.copy({std::string(press->key)}).empty());
    CHECK_FALSE(document.check(std::string(log->key), "exec", std::string(press->key), "exec").empty());
    CHECK(after(document, document.remove("", {std::string(press->key)})) == body + ")\n");
    const Document logicOnly(body + ")\n", env, {});
    CHECK(titled(logicOnly.view(""), "默认动作") == nullptr);
}

TEST_CASE("ADR-0091 00:5x blocks placed in a component's own file press where the component sits")
{
    FakeLabels named = labels();
    named.chinese["node:defaultAction"] = "默认动作";
    named.chinese["pin:ownPosition"] = "本组件位置";
    Environment env = environment();
    env.labels = &named;
    env.fileName = "controls/开火.ts";
    env.ownControl = "开火";
    const Document document("controls.开火.onPress(async () => {\n}, { consume: false })\n", env, {});
    REQUIRE(document.ok());
    const Graph page = document.view("");
    const GraphNode* press = titled(page, "默认动作");
    REQUIRE(press != nullptr);
    const Outcome added = document.add("", "call:touch.tap", 400, 0, std::string(press->key), "exec");
    const std::string text = after(document, added);
    CAPTURE(text);
    CHECK(text.find("touch.tap(controls.开火.position)") != std::string::npos);
    const Document next(text, env, added.layout, &document);
    const Graph graph = next.view("");
    const auto tap = std::ranges::find_if(graph.nodes, [](const GraphNode& node) {
        return node.key.find("touch.tap") != std::string_view::npos;
    });
    REQUIRE(tap != graph.nodes.end());
    CHECK(std::ranges::any_of(tap->inputs, [](const GraphPin& pin) { return pin.value == "本组件位置"; }));

    const Outcome swiped = document.add("", "call:touch.swipe", 400, 0, std::string(press->key), "exec");
    const std::string swipeText = after(document, swiped);
    CAPTURE(swipeText);
    CHECK(swipeText.find("touch.swipe(controls.开火.position, { x: 0.5, y: 0.3 })") != std::string::npos);
    const Document swipeDocument(swipeText, env, swiped.layout, &document);
    const Graph swipeGraph = swipeDocument.view("");
    const auto swipe = std::ranges::find_if(swipeGraph.nodes, [](const GraphNode& node) {
        return node.key.find("touch.swipe") != std::string_view::npos;
    });
    REQUIRE(swipe != swipeGraph.nodes.end());
    CHECK(swipe->problem.empty());
    CHECK(std::ranges::any_of(swipe->inputs,
                              [](const GraphPin& pin) { return pin.name == "from" && pin.value == "本组件位置"; }));
    CHECK(std::ranges::any_of(swipe->inputs, [](const GraphPin& pin) { return pin.name == "to" && pin.value == "50%, 30%"; }));

    const Document elsewhere("keys.F.onPress(async () => {\n})\n", environment(), {});
    const Outcome plain = elsewhere.add("", "call:touch.tap", 400, 0, "e:keys.F.onPress#0", "exec");
    CHECK(after(elsewhere, plain).find("touch.tap(0.5, 0.5)") != std::string::npos);
}

TEST_CASE("ADR-0091 00:5x the exported functions of library.ts are blocks in the other files, and placing one writes "
          "the import")
{
    const std::vector<gs::blueprint::LibraryFunction> library =
        gs::blueprint::libraryFunctions("export function aim(target: PicturePoint, times = 1) {\n}\n"
                                        "export async function burst(count: number) {\n}\n"
                                        "function hidden() {}\n"
                                        "export const value = 1;\n");
    REQUIRE(library.size() == 2);
    CHECK(library[0].name == "aim");
    REQUIRE(library[0].params.size() == 2);
    CHECK(library[0].params[0].name == "target");
    CHECK_FALSE(library[0].params[0].optional);
    CHECK(library[0].params[1].name == "times");
    CHECK(library[0].params[1].optional);
    CHECK_FALSE(library[0].async);
    CHECK(library[1].name == "burst");
    CHECK(library[1].async);
    CHECK(gs::blueprint::libraryFunctions("export function broken( {").empty());
    CHECK(gs::blueprint::libraryImportFor("logic.ts") == "./library.ts");
    CHECK(gs::blueprint::libraryImportFor("controls/fire.ts") == "../library.ts");
    CHECK(gs::blueprint::libraryImportFor("library.ts").empty());

    FakeLabels named = labels();
    named.chinese["badge:library"] = "函数库";
    named.chinese["page:library"] = "函数库";
    named.chinese["section:library"] = "函数库";
    Environment env = environment();
    env.labels = &named;
    env.library = library;
    env.fileName = "logic.ts";
    env.libraryImport = gs::blueprint::libraryImportFor(env.fileName);
    const Document document("keys.F.onPress(async () => {\n})\n", env, {});
    REQUIRE(document.ok());
    bool listed = false;
    for (const Json& item : document.catalog("", "", "", "")) {
        listed = listed || item["id"] == "call:lib:burst";
    }
    CHECK(listed);
    bool section = false;
    for (const Json& each : document.sidebar()) {
        section = section || (each["key"] == "library" && each["items"].size() == 2 && each["items"][0]["key"] == "lib:aim");
    }
    CHECK(section);

    const Outcome added = document.add("", "call:lib:burst", 400, 0, "e:keys.F.onPress#0", "exec");
    const std::string text = after(document, added);
    CAPTURE(text);
    CHECK(text.starts_with("import { burst } from \"./library.ts\"\n\nkeys.F.onPress("));
    CHECK(text.find("  await burst(undefined)\n") != std::string::npos);
    const Document next(text, env, added.layout, &document);
    const Graph graph = next.view("");
    const GraphNode* call = titled(graph, "burst");
    REQUIRE(call != nullptr);
    CHECK(call->badge == "函数库");
    CHECK(std::ranges::any_of(call->inputs, [](const GraphPin& pin) { return pin.name == "count" && !pin.optional; }));
    CHECK(next.open(std::string(call->key)) == "file:library.ts");

    const Outcome second = next.add("", "call:lib:aim", 400, 0, std::string(call->key), "exec");
    const std::string both = after(next, second);
    CAPTURE(both);
    CHECK(both.starts_with("import { burst, aim } from \"./library.ts\"\n"));
    CHECK(both.find("  aim(undefined)\n") != std::string::npos);
    const Document third(both, env, second.layout, &next);
    const Graph thirdGraph = third.view("");
    const GraphNode* aim = titled(thirdGraph, "aim");
    REQUIRE(aim != nullptr);
    CHECK(std::ranges::any_of(aim->inputs, [](const GraphPin& pin) { return pin.name == "times" && pin.optional; }));

    const Outcome floated = document.add("", "call:lib:aim", 400, 600, "", "");
    const std::string floatedText = after(document, floated);
    CAPTURE(floatedText);
    CHECK(floatedText.starts_with("import { aim } from \"./library.ts\"\n"));
    const Document floating(floatedText, env, floated.layout, &document);
    const Graph floatingGraph = floating.view("");
    const GraphNode* loose = titled(floatingGraph, "aim");
    REQUIRE(loose != nullptr);
    CHECK(loose->floating);
    CHECK(loose->badge == "函数库");

    const Document elsewhere("import { aim } from \"./other.ts\"\nkeys.F.onPress(() => {\n  aim(1)\n})\n", env, {});
    const Graph elsewhereGraph = elsewhere.view("");
    const GraphNode* other = titled(elsewhereGraph, "aim");
    REQUIRE(other != nullptr);
    CHECK(other->badge != "函数库");

    Environment inside = env;
    inside.fileName = "library.ts";
    inside.libraryImport = gs::blueprint::libraryImportFor(inside.fileName);
    const Document own("export function aim(target) {\n}\n", inside, {});
    REQUIRE(own.ok());
    CHECK(own.pages()[0]["title"] == "函数库");
    bool self = false;
    for (const Json& item : own.catalog("", "", "", "")) {
        self = self || item["id"].get<std::string>().starts_with("call:lib:");
    }
    CHECK_FALSE(self);
    const Outcome made = own.addType("function", "");
    CHECK(after(own, made) == "export function aim(target) {\n}\n\nexport function newFunction() {\n}\n");
    CHECK(made.page == "fn:newFunction");
    const Outcome plain = document.addType("function", "");
    CHECK(after(document, plain).ends_with("\n\nfunction newFunction() {\n}\n"));
}

TEST_CASE("BP-031 a second wire from one exec output makes both runs go together (issue 16)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await touch.tap(0.5, 0.5)\n"
                               "  await controls.开火.press()\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string tap = key(graph, "点某处");
    const Outcome pasted = document.paste("", "await sleep(100)", 40, 400);
    REQUIRE(pasted.ok());
    const Document floating(after(document, pasted), environment(), pasted.layout, &document);
    const Graph floatingGraph = floating.view("");
    const GraphNode* wait = titled(floatingGraph, "等待");
    REQUIRE(wait != nullptr);
    REQUIRE(wait->floating);
    const Outcome joined = floating.connect(tap, "exec", std::string(wait->key), "exec");
    REQUIRE(joined.ok());
    CHECK(after(floating, joined)
          == "keys.F.onPress(async () => {\n"
             "  await touch.tap(0.5, 0.5)\n"
             "  await parallel(async () => {\n"
             "    await controls.开火.press()\n"
             "  }, async () => {\n"
             "    await sleep(100)\n"
             "  })\n"
             "})\n");
    const Document together(after(floating, joined), environment(), joined.layout, &floating);
    const Graph fan = together.view("");
    CHECK(titled(fan, "parallel") == nullptr);
    std::vector<std::string> wires;
    for (const GraphWire& wire : fan.wires) {
        if (wire.from == tap && wire.fromPin == "exec") {
            wires.push_back(std::string(wire.key));
        }
    }
    REQUIRE(wires.size() == 2);
    CHECK(fan.wires.size() == 3);
    CHECK(together.layout().pages.at("").floating.empty());
    const GraphNode* fire = titled(fan, "开火 · 按下");
    const GraphNode* runWait = titled(fan, "等待");
    REQUIRE(fire != nullptr);
    REQUIRE(runWait != nullptr);
    CHECK_FALSE(runWait->floating);

    const Outcome third = together.paste("", "await log.info(\"tick\")", 40, 600);
    REQUIRE(third.ok());
    const Document three(after(together, third), environment(), third.layout, &together);
    const Graph threeGraph = three.view("");
    const GraphNode* log = titled(threeGraph, "log.info");
    REQUIRE(log != nullptr);
    const Outcome more = three.connect(tap, "exec", std::string(log->key), "exec");
    REQUIRE(more.ok());
    CHECK(after(three, more)
          == "keys.F.onPress(async () => {\n"
             "  await touch.tap(0.5, 0.5)\n"
             "  await parallel(async () => {\n"
             "    await controls.开火.press()\n"
             "  }, async () => {\n"
             "    await sleep(100)\n"
             "  }, async () => {\n"
             "    await log.info(\"tick\")\n"
             "  })\n"
             "})\n");

    const Document threeFan(after(three, more), environment(), more.layout, &three);
    const Graph fanGraph = threeFan.view("");
    std::vector<std::string> fanWires;
    for (const GraphWire& wire : fanGraph.wires) {
        if (wire.from == tap && wire.fromPin == "exec") {
            fanWires.push_back(std::string(wire.key));
        }
    }
    REQUIRE(fanWires.size() == 3);
    const Outcome middle = threeFan.removeWire(fanWires[1]);
    REQUIRE(middle.ok());
    CHECK(after(threeFan, middle)
          == "keys.F.onPress(async () => {\n"
             "  await touch.tap(0.5, 0.5)\n"
             "  await parallel(async () => {\n"
             "    await controls.开火.press()\n"
             "  }, async () => {\n"
             "    await log.info(\"tick\")\n"
             "  })\n"
             "})\n");
    const Document floated(after(threeFan, middle), environment(), middle.layout, &threeFan);
    const Graph floatedGraph = floated.view("");
    const GraphNode* floatingWait = titled(floatedGraph, "等待");
    REQUIRE(floatingWait != nullptr);
    CHECK(floatingWait->floating);
    CHECK_FALSE(floated.layout().pages.at("").floating.empty());

    const Outcome one = floated.removeWire(fanWires[0]);
    REQUIRE(one.ok());
    CHECK(after(floated, one)
          == "keys.F.onPress(async () => {\n"
             "  await touch.tap(0.5, 0.5)\n"
             "  await log.info(\"tick\")\n"
             "})\n");
    const Document plain(after(floated, one), environment(), one.layout, &floated);
    CHECK(titled(plain.view(""), "parallel") == nullptr);
    CHECK(plain.view("").wires.size() == 2);
}

TEST_CASE("BP-031 deleting every block of one branch drops that arrow; disconnecting drops them all (issue 16)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await touch.tap(0.5, 0.5)\n"
                               "  await parallel(async () => {\n"
                               "    await sleep(100)\n"
                               "  }, async () => {\n"
                               "    await controls.开火.press()\n"
                               "  }, async () => {\n"
                               "    await log.info(\"tick\")\n"
                               "  })\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Graph graph = document.view("");
    CHECK(titled(graph, "parallel") == nullptr);
    CHECK(graph.wires.size() == 4);
    const Outcome removed = document.remove("", {key(graph, "等待")});
    REQUIRE(removed.ok());
    CHECK(after(document, removed)
          == "keys.F.onPress(async () => {\n"
             "  await touch.tap(0.5, 0.5)\n"
             "  await parallel(async () => {\n"
             "    await controls.开火.press()\n"
             "  }, async () => {\n"
             "    await log.info(\"tick\")\n"
             "  })\n"
             "})\n");
    const Document shorter(after(document, removed), environment(), removed.layout, &document);
    Graph shorterGraph = shorter.view("");
    CHECK(titled(shorterGraph, "等待") == nullptr);
    CHECK(shorterGraph.wires.size() == 3);

    const std::string pair = "keys.F.onPress(async () => {\n"
                             "  await touch.tap(0.5, 0.5)\n"
                             "  await parallel(async () => {\n"
                             "    await sleep(100)\n"
                             "  }, async () => {\n"
                             "    await log.info(\"tick\")\n"
                             "  })\n"
                             "})\n";
    const Document two(pair, environment(), {});
    const Outcome unwrap = two.remove("", {key(two.view(""), "log.info")});
    REQUIRE(unwrap.ok());
    CHECK(after(two, unwrap)
          == "keys.F.onPress(async () => {\n"
             "  await touch.tap(0.5, 0.5)\n"
             "  await sleep(100)\n"
             "})\n");

    const Outcome all = document.disconnect(key(graph, "点某处"), "exec");
    REQUIRE(all.ok());
    CHECK(after(document, all)
          == "keys.F.onPress(async () => {\n"
             "  await touch.tap(0.5, 0.5)\n"
             "})\n");
    const Document cut(after(document, all), environment(), all.layout, &document);
    CHECK(cut.layout().pages.at("").floating.size() == 3);
    CHECK(cut.view("").wires.size() == 1);
}

TEST_CASE("BP-029 a floating block dropped on a wire goes between the two blocks and the rest makes room (issue 33e)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  await controls.开火.press()\n"
                               "})\n";
    Layout layout;
    layout.pages[""].floating.push_back({.key = "f1", .code = "await sleep(100)", .x = 40, .y = 300});
    const Document document(source, environment(), layout);
    const Graph graph = document.view("");
    const std::string pick = key(graph, "拾取 · 点一下");
    const std::string fire = key(graph, "开火 · 按下");
    const GraphNode* wait = titled(graph, "等待");
    REQUIRE(wait != nullptr);
    CHECK(wait->floating);
    const GraphWire* link = nullptr;
    for (const GraphWire& wire : graph.wires) {
        if (wire.from == pick && wire.fromPin == "exec" && wire.to == fire && wire.toPin == "exec") {
            link = &wire;
        }
    }
    REQUIRE(link != nullptr);
    const GraphNode* beforeNode = titled(graph, "开火 · 按下");
    REQUIRE(beforeNode != nullptr);
    const double before = beforeNode->x;
    const Outcome outcome = document.insertOnWire(link->key, std::string(wait->key));
    CHECK(after(document, outcome)
          == "keys.F.onPress(async () => {\n  await controls.拾取.tap()\n  await sleep(100)\n  await controls.开火.press()\n})\n");
    CHECK_FALSE(outcome.key.empty());
    CHECK(outcome.layout.pages.at("").floating.empty());

    const Document next(after(document, outcome), environment(), outcome.layout);
    const Graph moved = next.view("");
    const GraphNode* after1 = titled(moved, "开火 · 按下");
    REQUIRE(after1 != nullptr);
    CHECK_FALSE(after1->floating);
    CHECK(after1->x > before);
    const GraphNode* placed = titled(moved, "等待");
    REQUIRE(placed != nullptr);
    const GraphNode* pickAgain = titled(moved, "拾取 · 点一下");
    REQUIRE(pickAgain != nullptr);
    CHECK_FALSE(placed->floating);
    CHECK(placed->x > pickAgain->x);
    CHECK(placed->x < after1->x);
}

TEST_CASE("BP-029 a block added through a wire is put between the two and the rest makes room (issue 33e)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  await controls.开火.press()\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const GraphNode* fireBefore = titled(graph, "开火 · 按下");
    REQUIRE(fireBefore != nullptr);
    const double before = fireBefore->x;
    const Outcome outcome = document.add("", "call:touch.tap", 900, 400, key(graph, "拾取 · 点一下"), "exec");
    CHECK(after(document, outcome)
          == "keys.F.onPress(async () => {\n  await controls.拾取.tap()\n  await touch.tap(0.5, 0.5)\n"
             "  await controls.开火.press()\n})\n");
    REQUIRE_FALSE(outcome.key.empty());
    const Document next(after(document, outcome), environment(), outcome.layout);
    const Graph moved = next.view("");
    const GraphNode* fire = titled(moved, "开火 · 按下");
    REQUIRE(fire != nullptr);
    CHECK(fire->x > before);
    const GraphNode* tap = titled(moved, "点某处");
    REQUIRE(tap != nullptr);
    const GraphNode* pickNode = titled(moved, "拾取 · 点一下");
    REQUIRE(pickNode != nullptr);
    CHECK(tap->x > pickNode->x);
    CHECK(tap->x < fire->x);
}

TEST_CASE("BP-029 a whole floating run moves onto a wire together (issue 33e)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  await controls.开火.press()\n"
                               "})\n";
    Layout layout;
    layout.pages[""].floating.push_back(
        {.key = "f1", .code = "await sleep(100)\nawait touch.tap(0.2, 0.3)", .x = 40, .y = 300});
    const Document document(source, environment(), layout);
    const Graph graph = document.view("");
    const std::string pick = key(graph, "拾取 · 点一下");
    const GraphWire* link = nullptr;
    for (const GraphWire& wire : graph.wires) {
        if (wire.from == pick && wire.fromPin == "exec") {
            link = &wire;
        }
    }
    REQUIRE(link != nullptr);
    const Outcome outcome = document.insertOnWire(link->key, key(graph, "等待"));
    CHECK(after(document, outcome)
          == "keys.F.onPress(async () => {\n  await controls.拾取.tap()\n  await sleep(100)\n"
             "  await touch.tap(0.2, 0.3)\n  await controls.开火.press()\n})\n");
    const Document next(after(document, outcome), environment(), outcome.layout);
    const Graph moved = next.view("");
    const GraphNode* end = titled(moved, "开火 · 按下");
    const GraphNode* tap = titled(moved, "点某处");
    REQUIRE(end != nullptr);
    REQUIRE(tap != nullptr);
    CHECK(end->x > tap->x);
}

TEST_CASE("BP-029 only a block on its own can be dropped on a wire (issue 33e)")
{
    const std::string source = "keys.F.onPress(async () => {\n"
                               "  await controls.拾取.tap()\n"
                               "  await controls.开火.press()\n"
                               "})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string pick = key(graph, "拾取 · 点一下");
    const GraphWire* link = nullptr;
    for (const GraphWire& wire : graph.wires) {
        if (wire.from == pick && wire.fromPin == "exec") {
            link = &wire;
        }
    }
    REQUIRE(link != nullptr);
    const Outcome refused = document.insertOnWire(link->key, pick);
    CHECK_FALSE(refused.ok());
    CHECK(refused.reason == "只有没接任何线的方块才能放到线上");
}

TEST_CASE("BP-030 a wire from a find to a floating tap puts the tap in the branch the value lives in")
{
    const std::string source = "keys.G.onPress(async () => {\n"
                               "  const 位置 = await vision.find(res.拾取)\n"
                               "  if (位置) {\n"
                               "  }\n"
                               "})\n";
    Layout layout;
    layout.pages[""].floating.push_back({.key = "f1", .code = "await touch.tap(0.5, 0.5)", .x = 40, .y = 300});
    const Document document(source, environment(), layout);
    const Graph graph = document.view("");
    const GraphNode* find = titled(graph, "找图");
    const GraphNode* tap = titled(graph, "点某处");
    REQUIRE(find != nullptr);
    REQUIRE(tap != nullptr);
    CHECK(tap->floating);
    CHECK(document.check(std::string(find->key), "位置", std::string(tap->key), "x").empty());
    const Outcome outcome = document.connect(std::string(find->key), "位置", std::string(tap->key), "x");
    CHECK(after(document, outcome) == "keys.G.onPress(async () => {\n"
                                      "  const 位置 = await vision.find(res.拾取)\n"
                                      "  if (位置) {\n"
                                      "    await touch.tap(位置)\n"
                                      "  }\n"
                                      "})\n");
    CHECK(outcome.layout.pages.at("").floating.empty());
    CHECK_FALSE(outcome.key.empty());
    const Document next(after(document, outcome), environment(), outcome.layout);
    const Graph moved = next.view("");
    const GraphNode* placed = titled(moved, "点某处");
    REQUIRE(placed != nullptr);
    const GraphNode* findNode = titled(moved, "找图");
    REQUIRE(findNode != nullptr);
    CHECK_FALSE(placed->floating);
    CHECK(placed->x > findNode->x);
}

TEST_CASE("BP-031 floating code that does not parse shows as a code block and can be fixed")
{
    const std::string source = "keys.F.onPress(async () => {\n  await controls.拾取.tap()\n})\n";
    Layout layout;
    layout.pages[""].floating.push_back({.key = "f1", .code = "await sleep(", .x = 40, .y = 300});
    const Document document(source, environment(), layout);
    const Graph graph = document.view("");
    const GraphNode* broken = nullptr;
    for (const GraphNode& node : graph.nodes) {
        if (node.kind == "code") {
            broken = &node;
        }
    }
    REQUIRE(broken != nullptr);
    CHECK(broken->floating);
    CHECK(broken->problem.find("这段游离代码有错") != std::string_view::npos);
    CHECK(broken->code == "await sleep(");
    CHECK(broken->x == 40);
    CHECK_FALSE(document.setCode(std::string(broken->key), "await sleep(").ok());

    const Outcome fixed = document.setCode(std::string(broken->key), "await sleep(100)");
    REQUIRE_MESSAGE(fixed.ok(), fixed.reason);
    CHECK(after(document, fixed) == source);
    const Document next(source, environment(), fixed.layout);
    const Graph moved = next.view("");
    const GraphNode* wait = titled(moved, "等待");
    REQUIRE(wait != nullptr);
    CHECK(wait->floating);
    CHECK(wait->problem.empty());
}

TEST_CASE("BP-070 a new variable is written at the top of the file with its type, default and comment (issue 31)")
{
    const std::string source = "keys.F.onPress(async () => {\n  await sleep(100)\n})\n";
    const Document document(source, environment(), {});
    const Outcome plain = document.createVariable("", {{"name", "血量"}, {"kind", "integer"}, {"value", "100"}});
    CHECK(after(document, plain) == "let 血量: number = 100\n" + source);

    const Outcome documented = document.createVariable("", {{"name", "血量"}, {"kind", "integer"}, {"value", "100"},
                                                            {"category", "战斗"}, {"description", "英雄的血量"}});
    CHECK(after(document, documented) == "/** 英雄的血量 @category 战斗 */\nlet 血量: number = 100\n" + source);

    const Outcome points =
        document.createVariable("", {{"name", "目标"}, {"kind", "point"}, {"container", "list"},
                                     {"value", "[{ x: 0.5, y: 0.5 }]"}, {"category", "Battle"}});
    CHECK(after(document, points)
          == "/** @category Battle */\nlet 目标: PicturePoint[] = [{ x: 0.5, y: 0.5 }]\n" + source);

    const Outcome named = document.createVariable("", {{"name", "计数"}, {"kind", "integer"}, {"container", "dict"}});
    CHECK(after(document, named) == "let 计数: Map<string, number> = new Map<string, number>()\n" + source);

    const Outcome kept = document.createVariable("", {{"name", "最高分"}, {"kind", "integer"}, {"keep", true}});
    CHECK(after(document, kept)
          == "/** @keep */\nlet 最高分: number = (store.get(\"最高分\") as number) ?? 0\n" + source);

    const Outcome decimal = document.createVariable("", {{"name", "速度"}, {"kind", "decimal"}, {"value", "1.5"}});
    CHECK(after(document, decimal) == "/** @decimal */\nlet 速度: number = 1.5\n" + source);
}

TEST_CASE("BP-071 a variable of a function is written at the top of the function body (issue 31)")
{
    const std::string source = "async function attack() {\n  await sleep(100)\n}\n";
    const Document document(source, environment(), {});
    const Outcome made = document.createVariable("fn:attack", {{"name", "次数"}, {"kind", "integer"},
                                                               {"scope", "local"}});
    CHECK(after(document, made) == "async function attack() {\n  let 次数: number = 0\n  await sleep(100)\n}\n");
}

TEST_CASE("BP-072 a variable of a component is a field and a public property when it can be changed in the panel "
          "(issue 31)")
{
    const std::string source = "export default class ToggleHold extends Component {\n"
                               "    static props = {\n"
                               "        key: prop.key(\"F\", { label: \"Key\" }),\n"
                               "    };\n"
                               "\n"
                               "    holding = false;\n"
                               "}\n";
    const Document document(source, environment("component.ts"), {});
    const Outcome made = document.createVariable("", {{"name", "血量"}, {"kind", "integer"}, {"value", "100"},
                                                      {"scope", "class"}, {"editable", true}});
    CHECK(after(document, made)
          == "export default class ToggleHold extends Component {\n"
             "    /** @editable */\n"
             "    血量: number = this.props.血量;\n"
             "    static props = {\n"
             "        key: prop.key(\"F\", { label: \"Key\" }),\n"
             "        血量: prop.number(100, { label: \"血量\" })\n"
             "    };\n"
             "\n"
             "    holding = false;\n"
             "}\n");
}

TEST_CASE("BP-073 renaming a variable changes only the uses of that binding (issue 31)")
{
    const std::string source = "let 血量 = 1\n"
                               "keys.F.onPress(async () => {\n"
                               "  let 血量 = 2\n"
                               "  log.info(血量)\n"
                               "  log.info(this.血量)\n"
                               "})\n"
                               "log.info(血量)\n";
    const Document document(source, environment(), {});
    const Outcome renamed = document.saveVariable("", "variable:血量",
                                                  {{"name", "生命"}, {"kind", "integer"}, {"value", "1"}});
    CHECK(after(document, renamed) == "let 生命: number = 1\n"
                                      "keys.F.onPress(async () => {\n"
                                      "  let 血量 = 2\n"
                                      "  log.info(血量)\n"
                                      "  log.info(this.血量)\n"
                                      "})\n"
                                      "log.info(生命)\n");
}

TEST_CASE("BP-074 every setting of a variable reads back from the code (issue 31)")
{
    const std::string source = "keys.F.onPress(async () => {\n  await sleep(100)\n})\n";
    const Document document(source, environment(), {});
    const std::vector<nlohmann::json> specs = {
        {{"name", "开关"}, {"kind", "toggle"}, {"keep", true}},
        {{"name", "血量"}, {"kind", "integer"}, {"value", "100"}, {"category", "战斗"}, {"description", "血量"}},
        {{"name", "速度"}, {"kind", "decimal"}, {"value", "1.5"}},
        {{"name", "名字"}, {"kind", "text"}, {"value", "\"亚瑟\""}},
        {{"name", "时长"}, {"kind", "duration"}, {"value", "800"}},
        {{"name", "目标"}, {"kind", "point"}, {"container", "list"}, {"value", "[]"}},
        {{"name", "计数"}, {"kind", "integer"}, {"container", "dict"}, {"keyKind", "number"}},
        {{"name", "见过"}, {"kind", "text"}, {"container", "set"}},
    };
    for (const nlohmann::json& spec : specs) {
        const std::string name = spec["name"].get<std::string>();
        const Outcome made = document.createVariable("", spec);
        REQUIRE_MESSAGE(made.ok(), made.reason);
        const Document next(after(document, made), environment(), {});
        REQUIRE(next.ok());
        const Json back = next.variable("", "variable:" + name);
        REQUIRE_MESSAGE(!back.is_null(), name);
        for (const char* field : {"name", "kind", "container", "keyKind", "value", "category", "description"}) {
            const auto wanted = spec.find(std::string(field));
            const std::string expected = wanted != spec.end() && wanted->is_string() ? wanted->get<std::string>()
                                                                                     : std::string();
            if (expected.empty()) {
                continue;
            }
            INFO(name << " " << field);
            CHECK(back[std::string(field)].get<std::string>() == expected);
        }
        CHECK(back["keep"].get<bool>() == (spec.contains("keep") && spec["keep"].get<bool>()));
        CHECK(back["scope"].get<std::string>() == "file");
    }
}

TEST_CASE("BP-075 promoting a pin makes a variable with the pin's value and connects it in one step (issue 31)")
{
    const std::string source = "keys.F.onPress(async () => {\n  if (true) {\n    await sleep(100)\n  }\n})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string branch = key(graph, "判断");
    const Outcome made = document.promoteToVariable("", branch, "condition");
    CHECK(after(document, made) == "let 条件: boolean = true\n"
                                   "keys.F.onPress(async () => {\n  if (条件) {\n    await sleep(100)\n  }\n})\n");
    const Document next(after(document, made), environment(), {});
    const Graph nextGraph = next.view("");
    const GraphNode* again = titled(nextGraph, "判断");
    REQUIRE(again != nullptr);
    const GraphPin* condition = nullptr;
    for (const GraphPin& pin : again->inputs) {
        if (pin.name == "condition") {
            condition = &pin;
        }
    }
    REQUIRE(condition != nullptr);
    CHECK(condition->connected);
}

TEST_CASE("BP-076 dragging a variable out makes a get or a set of it (issue 31)")
{
    const std::string source = "let 次数 = 0\nkeys.F.onPress(async () => {\n  await sleep(100)\n})\n";
    const Document document(source, environment(), {});
    const Graph graph = document.view("");
    const std::string wait = key(graph, "等待");
    const Outcome got = document.drop("", {{"item", "variable:次数"}, {"mode", "get"}}, 400, 40, wait, "ms");
    CHECK(after(document, got) == "let 次数 = 0\nkeys.F.onPress(async () => {\n  await sleep(次数)\n})\n");
    const Outcome set = document.drop("", {{"item", "variable:次数"}, {"mode", "set"}}, 400, 40, wait, "exec");
    CHECK(after(document, set) == "let 次数 = 0\nkeys.F.onPress(async () => {\n  await sleep(100)\n  次数 = 0\n})\n");
}

TEST_CASE("BP-077 a variable that is still used cannot be deleted (issue 31)")
{
    const std::string unused = "let 次数 = 0\nkeys.F.onPress(async () => {\n  await sleep(100)\n})\n";
    const Document spare(unused, environment(), {});
    CHECK(after(spare, spare.deleteVariable("", "variable:次数"))
          == "keys.F.onPress(async () => {\n  await sleep(100)\n})\n");

    const std::string used = "let 次数 = 0\nkeys.F.onPress(async () => {\n  await sleep(次数)\n})\n";
    const Document busy(used, environment(), {});
    CHECK(busy.deleteVariable("", "variable:次数").reason == "“次数”还在用，改掉它先");
}
