#include "Fixture.h"
#include "Types.h"

#include <gs/blueprint/Text.h>

#include <doctest/doctest.h>

#include <string>

using namespace gs::blueprint;
using namespace blueprint_test;

namespace {

std::string after(const Document& document, const Outcome& outcome)
{
    REQUIRE_MESSAGE(outcome.ok(), outcome.reason);
    return applied(document.source(), outcome.changes);
}

const Json* sectionOf(const Json& sidebar, std::string_view key)
{
    for (const Json& section : sidebar) {
        if (same(section["key"], key)) {
            return &section;
        }
    }
    return nullptr;
}

const Json* memberOf(const Json* section, std::string_view key)
{
    if (section == nullptr) {
        return nullptr;
    }
    for (const Json& item : (*section)["items"]) {
        if (same(item["key"], key)) {
            return &item;
        }
    }
    return nullptr;
}

const GraphNode* findNode(const Graph& graph, std::string_view title)
{
    for (const GraphNode& node : graph.nodes) {
        if (node.title == title) {
            return &node;
        }
    }
    return nullptr;
}

const GraphPin* outputOf(const GraphNode& node, std::string_view name)
{
    for (const GraphPin& pin : node.outputs) {
        if (pin.name == name) {
            return &pin;
        }
    }
    return nullptr;
}

bool wiredGraph(const Graph& graph, const GraphNode* from, std::string_view fromPin, const GraphNode* to,
                std::string_view toPin)
{
    if (from == nullptr || to == nullptr) {
        return false;
    }
    for (const GraphWire& wire : graph.wires) {
        if (wire.from == from->key && wire.fromPin == fromPin && wire.to == to->key && wire.toPin == toPin) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("BP-090 the type scanner reads interfaces, type aliases and enums and nothing else")
{
    const std::string source = "// interface 假的 { x: number }\n"
                               "const 普通 = { a: 1 }\n"
                               "export interface 位置 {\n"
                               "  x: number\n"
                               "  名字?: string\n"
                               "}\n"
                               "type 区域 = { x: number; y: number; width: number }\n"
                               "/* const 假的 = { a: \"a\" } as const */\n"
                               "const 阵营 = { 红: \"红\", 蓝: \"蓝\" } as const\n"
                               "type 阵营 = typeof 阵营[keyof typeof 阵营]\n"
                               "class 敌人 {\n"
                               "  interface 不在里面 { y: number }\n"
                               "}\n";
    const UserTypes types = scanTypes(source);
    REQUIRE(types.structs.size() == 2);
    CHECK(types.structs[0].name == "位置");
    REQUIRE(types.structs[0].fields.size() == 2);
    CHECK(types.structs[0].fields[0].name == "x");
    CHECK(types.structs[0].fields[0].type == "number");
    CHECK_FALSE(types.structs[0].fields[0].optional);
    CHECK(types.structs[0].fields[1].name == "名字");
    CHECK(types.structs[0].fields[1].type == "string");
    CHECK(types.structs[0].fields[1].optional);
    CHECK_FALSE(types.structs[0].alias);
    CHECK(types.structs[1].name == "区域");
    CHECK(types.structs[1].alias);
    CHECK(types.structs[1].fields.size() == 3);
    REQUIRE(types.enums.size() == 1);
    CHECK(types.enums[0].name == "阵营");
    REQUIRE(types.enums[0].options.size() == 2);
    CHECK(types.enums[0].options[0] == "红");
    CHECK(types.enums[0].options[1] == "蓝");
    const std::string span = source.substr(types.enums[0].start, types.enums[0].end - types.enums[0].start);
    CHECK(span.find("as const") != std::string::npos);
    CHECK(span.find("type 阵营") != std::string::npos);
}

TEST_CASE("BP-091 a structure shows in the sidebar, makes a make block and turns a field read into a break block")
{
    const std::string source = "interface 位置 {\n"
                               "  x: number\n"
                               "  y: number\n"
                               "}\n"
                               "keys.F.onPress(async () => {\n"
                               "  const 起点 = { x: 0.5, y: 0.5 }\n"
                               "  await touch.tap(起点.x, 起点.y)\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE_MESSAGE(document.ok(), document.problem());
    CHECK(document.source() == source);

    const Json sidebar = document.sidebar();
    const Json* structs = sectionOf(sidebar, "structs");
    REQUIRE(structs != nullptr);
    CHECK((*structs)["title"] == "结构");
    const Json* position = memberOf(structs, "def:位置");
    REQUIRE(position != nullptr);
    CHECK((*position)["title"] == "位置");
    REQUIRE((*position)["members"].size() == 2);
    CHECK((*position)["members"][0]["title"] == "x");
    CHECK((*position)["members"][0]["badge"] == "number");

    const Json catalog = document.catalog("", "", "", "");
    bool makes = false;
    bool breaks = false;
    for (const Json& entry : catalog) {
        makes = makes || same(entry["id"], "make:位置");
        breaks = breaks || same(entry["id"], "break:位置.y");
    }
    CHECK(makes);
    CHECK(breaks);

    const Graph graph = document.view("");
    const GraphNode* make = findNode(graph, "合成 位置");
    REQUIRE(make != nullptr);
    CHECK(make->outputs.front().type == "struct");
    bool hasX = false;
    for (const GraphPin& pin : make->inputs) {
        hasX = hasX || pin.name == "x";
    }
    CHECK(hasX);
    const GraphNode* broken = findNode(graph, "拆开 位置 · x");
    REQUIRE(broken != nullptr);
    CHECK(broken->outputs.front().type == "number");
    REQUIRE(broken->inputs.size() == 1);
    CHECK(broken->inputs.front().type == "struct");
}

TEST_CASE("BP-092 an enum shows in the sidebar and its member read and switch become blocks")
{
    const std::string source = "const 阵营 = { 红: \"红\", 蓝: \"蓝\" } as const\n"
                               "type 阵营 = typeof 阵营[keyof typeof 阵营]\n"
                               "keys.F.onPress(async () => {\n"
                               "  log.info(阵营.红)\n"
                               "  switch (阵营.蓝) {\n"
                               "    case 阵营.红:\n"
                               "      await sleep(100)\n"
                               "      break\n"
                               "    case 阵营.蓝:\n"
                               "      break\n"
                               "  }\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE_MESSAGE(document.ok(), document.problem());
    CHECK(document.source() == source);

    const Json sidebar = document.sidebar();
    const Json* enums = sectionOf(sidebar, "enums");
    REQUIRE(enums != nullptr);
    const Json* camp = memberOf(enums, "def:阵营");
    REQUIRE(camp != nullptr);
    REQUIRE((*camp)["members"].size() == 2);
    CHECK((*camp)["members"][0]["title"] == "红");

    const Graph graph = document.view("");
    const GraphNode* value = findNode(graph, "阵营 · 红");
    REQUIRE(value != nullptr);
    CHECK(value->outputs.front().type == "enum");
    const GraphNode* branch = findNode(graph, "按选项分支 阵营");
    REQUIRE(branch != nullptr);
    CHECK(outputOf(*branch, "case:红") != nullptr);
    CHECK(outputOf(*branch, "case:蓝") != nullptr);
    CHECK(outputOf(*branch, "default") != nullptr);
    const GraphNode* wait = findNode(graph, "等待");
    REQUIRE(wait != nullptr);
    CHECK(wiredGraph(graph, branch, "case:红", wait, "exec"));
}

TEST_CASE("BP-093 a plain class shows in the sidebar, keeps its constructor page, marks private members and casts")
{
    const std::string source = "class 敌人 {\n"
                               "  名字 = \"\"\n"
                               "  private 生命 = 100\n"
                               "  constructor(名字) {\n"
                               "    this.名字 = 名字\n"
                               "  }\n"
                               "  受伤(数值) {\n"
                               "    this.生命 -= 数值\n"
                               "  }\n"
                               "}\n"
                               "class 精英 extends 敌人 {\n"
                               "  受伤(数值) {\n"
                               "    super.受伤(数值)\n"
                               "  }\n"
                               "}\n"
                               "keys.F.onPress(async () => {\n"
                               "  const 一个 = new 精英(名字)\n"
                               "  if (一个 instanceof 敌人) {\n"
                               "    await sleep(100)\n"
                               "  }\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE_MESSAGE(document.ok(), document.problem());
    CHECK(document.source() == source);
    CHECK(document.hasPage("fn:敌人.constructor"));
    CHECK(document.hasPage("fn:敌人.受伤"));
    CHECK(document.hasPage("fn:精英.受伤"));

    const Json sidebar = document.sidebar();
    const Json* classes = sectionOf(sidebar, "classes");
    REQUIRE(classes != nullptr);
    const Json* enemy = memberOf(classes, "def:敌人");
    REQUIRE(enemy != nullptr);
    const Json* elite = memberOf(classes, "def:精英");
    REQUIRE(elite != nullptr);
    CHECK((*elite)["badge"] == "extends 敌人");

    const Json info = document.typeInfo("敌人");
    CHECK(info["kind"] == "class");
    bool foundPrivate = false;
    for (const Json& member : info["members"]) {
        if (same(member["name"], "生命")) {
            foundPrivate = member["private"] == true;
        }
    }
    CHECK(foundPrivate);
    REQUIRE(info["params"].size() == 1);
    CHECK(info["params"][0]["name"] == "名字");
    const Json bases = document.typeInfo("精英")["bases"];
    CHECK(bases.size() == 2);

    const Graph graph = document.view("");
    const GraphNode* made = findNode(graph, "新建 精英");
    REQUIRE(made != nullptr);
    CHECK(made->outputs.back().type == "object");
    const GraphNode* cast = findNode(graph, "转换为 敌人");
    REQUIRE(cast != nullptr);
    CHECK(outputOf(*cast, "is") != nullptr);
    CHECK(outputOf(*cast, "isNot") != nullptr);
    const GraphNode* wait = findNode(graph, "等待");
    REQUIRE(wait != nullptr);
    CHECK(wiredGraph(graph, cast, "is", wait, "exec"));
}

TEST_CASE("BP-094 a dispatcher declared in a plain class is drawn and can be bound and fired")
{
    const std::string source = "class 敌人 {\n"
                               "  倒下 = this.event()\n"
                               "  受伤(数值) {\n"
                               "  }\n"
                               "}\n"
                               "keys.F.onPress(async () => {\n"
                               "  const 一个 = new 敌人()\n"
                               "  const 订阅 = 一个.倒下.on(({ value }) => {\n"
                               "  })\n"
                               "  一个.倒下.emit(1)\n"
                               "  订阅.dispose()\n"
                               "})\n";
    const Document document(source, environment(), {});
    REQUIRE_MESSAGE(document.ok(), document.problem());
    CHECK(document.source() == source);
    const Json info = document.typeInfo("敌人");
    bool event = false;
    for (const Json& member : info["members"]) {
        event = event || (same(member["name"], "倒下") && same(member["kind"], "event"));
    }
    CHECK(event);
    const Graph graph = document.view("");
    CHECK(findNode(graph, "新建 敌人") != nullptr);
}

TEST_CASE("BP-095 adding a structure, an enum and a class writes the declarations the user could have typed")
{
    const std::string source = "keys.F.onPress(async () => {\n})\n";
    const Document document(source, environment(), {});
    const Outcome made = document.addType("struct", "位置");
    CHECK(made.key == "def:位置");
    CHECK(after(document, made)
          == source + "\ninterface 位置 {\n  字段: number\n}\n");

    const Outcome camp = document.addType("enum", "阵营");
    CHECK(camp.key == "def:阵营");
    CHECK(after(document, camp)
          == source + "\nconst 阵营 = {\n  选项: \"选项\",\n} as const\n"
                      "type 阵营 = typeof 阵营[keyof typeof 阵营]\n");

    const Outcome enemy = document.addType("class", "敌人");
    CHECK(enemy.key == "def:敌人");
    CHECK(after(document, enemy) == source + "\nclass 敌人 {\n}\n");

    const Document withPosition(after(document, made), environment(), {});
    CHECK_FALSE(withPosition.addType("struct", "位置").ok());
    CHECK_FALSE(document.addType("struct", "1 2").ok());
    CHECK_FALSE(document.addType("moon", "月").ok());
}

TEST_CASE("BP-096 editing a structure rewrites only its declaration")
{
    const std::string source = "interface 位置 {\n"
                               "  x: number\n"
                               "  y: number\n"
                               "}\n"
                               "keys.F.onPress(async () => {\n})\n";
    const Document document(source, environment(), {});
    CHECK(after(document, document.editType("位置", "addField", ""))
          == "interface 位置 {\n  x: number\n  y: number\n  字段: number\n}\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("位置", "field.x.type", "string"))
          == "interface 位置 {\n  x: string\n  y: number\n}\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("位置", "field.y.optional", true))
          == "interface 位置 {\n  x: number\n  y?: number\n}\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("位置", "field.y.name", "纵"))
          == "interface 位置 {\n  x: number\n  纵: number\n}\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("位置", "removeField.y"))
          == "interface 位置 {\n  x: number\n}\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("位置", "name", "地点"))
          == "interface 地点 {\n  x: number\n  y: number\n}\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("位置", "delete"))
          == "keys.F.onPress(async () => {\n})\n");
    CHECK_FALSE(document.editType("位置", "field.nope.type", "string").ok());
    CHECK_FALSE(document.editType("位置", "field.x.name", "y").ok());
}

TEST_CASE("BP-097 editing an enum rewrites its const object and its union type")
{
    const std::string source = "const 阵营 = {\n"
                               "  红: \"红\",\n"
                               "  蓝: \"蓝\",\n"
                               "} as const\n"
                               "type 阵营 = typeof 阵营[keyof typeof 阵营]\n"
                               "keys.F.onPress(async () => {\n})\n";
    const Document document(source, environment(), {});
    CHECK(after(document, document.editType("阵营", "addOption", ""))
          == "const 阵营 = {\n  红: \"红\",\n  蓝: \"蓝\",\n  选项: \"选项\",\n} as const\n"
             "type 阵营 = typeof 阵营[keyof typeof 阵营]\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("阵营", "option.红", "红方"))
          == "const 阵营 = {\n  红方: \"红方\",\n  蓝: \"蓝\",\n} as const\n"
             "type 阵营 = typeof 阵营[keyof typeof 阵营]\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("阵营", "removeOption.蓝"))
          == "const 阵营 = {\n  红: \"红\",\n} as const\n"
             "type 阵营 = typeof 阵营[keyof typeof 阵营]\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK(after(document, document.editType("阵营", "name", "队伍"))
          == "const 队伍 = {\n  红: \"红\",\n  蓝: \"蓝\",\n} as const\n"
             "type 队伍 = typeof 队伍[keyof typeof 队伍]\n"
             "keys.F.onPress(async () => {\n})\n");
    CHECK_FALSE(document.editType("阵营", "removeOption.绿").ok());
}

TEST_CASE("BP-098 editing a class adds and changes members without touching method bodies")
{
    const std::string source = "class 敌人 {\n"
                               "  受伤(数值) {\n"
                               "    this.生命 -= 数值\n"
                               "  }\n"
                               "}\n";
    const Document document(source, environment(), {});
    CHECK(after(document, document.editType("敌人", "addMember", ""))
          == "class 敌人 {\n  受伤(数值) {\n    this.生命 -= 数值\n  }\n  字段 = 0\n}\n");
    CHECK(after(document, document.editType("敌人", "addFunction", ""))
          == "class 敌人 {\n  受伤(数值) {\n    this.生命 -= 数值\n  }\n  newFunction() {\n  }\n}\n");
    CHECK(after(document, document.editType("敌人", "addEvent", ""))
          == "class 敌人 {\n  受伤(数值) {\n    this.生命 -= 数值\n  }\n  事件 = this.event()\n}\n");
    CHECK(after(document, document.editType("敌人", "member.受伤.private", true))
          == "class 敌人 {\n  private 受伤(数值) {\n    this.生命 -= 数值\n  }\n}\n");
    CHECK(after(document, document.editType("敌人", "member.受伤.private", false)) == source);
    CHECK(after(document, document.editType("敌人", "member.受伤.name", "被伤"))
          == "class 敌人 {\n  被伤(数值) {\n    this.生命 -= 数值\n  }\n}\n");
    CHECK(after(document, document.editType("敌人", "member.受伤.remove"))
          == "class 敌人 {\n}\n");
    CHECK(after(document, document.editType("敌人", "base", "生物"))
          == "class 敌人 extends 生物 {\n  受伤(数值) {\n    this.生命 -= 数值\n  }\n}\n");
    CHECK(after(document, document.editType("敌人", "addCtorParam", nlohmann::json{{"name", "名字"}}))
          == "class 敌人 {\n  受伤(数值) {\n    this.生命 -= 数值\n  }\n  constructor(名字) {\n  }\n}\n");
    CHECK(document.editType("敌人", "name", "士兵").ok());
}

TEST_CASE("BP-099 an enum's const object is a type, not a variable or a block that runs on load")
{
    const std::string source = "const 阵营 = {\n"
                               "  红: \"红\",\n"
                               "  蓝: \"蓝\",\n"
                               "} as const\n"
                               "type 阵营 = typeof 阵营[keyof typeof 阵营]\n"
                               "let 次数 = 0\n"
                               "keys.F.onPress(async () => {\n})\n";
    const Document document(source, environment(), {});
    REQUIRE_MESSAGE(document.ok(), document.problem());
    const Json sidebar = document.sidebar();
    const Json* vars = sectionOf(sidebar, "vars");
    REQUIRE(vars != nullptr);
    CHECK(memberOf(vars, "variable:阵营") == nullptr);
    CHECK(memberOf(vars, "variable:次数") != nullptr);
    CHECK(memberOf(sectionOf(sidebar, "enums"), "def:阵营") != nullptr);
    const Graph graph = document.view("");
    CHECK(findNode(graph, "设置 阵营") == nullptr);
    CHECK(findNode(graph, "设置 次数") != nullptr);
}
