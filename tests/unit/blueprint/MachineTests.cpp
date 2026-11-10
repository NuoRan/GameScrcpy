#include "Fixture.h"

#include <gs/blueprint/Text.h>

#include <doctest/doctest.h>

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

std::string keyOf(const Graph& graph, std::string_view title)
{
    const GraphNode* node = titled(graph, title);
    REQUIRE_MESSAGE(node != nullptr, std::string(title));
    return std::string(node->key);
}

const GraphWire* wireBetween(const Graph& graph, std::string_view from, std::string_view to)
{
    const std::string fromKey = keyOf(graph, from);
    const std::string toKey = keyOf(graph, to);
    for (const GraphWire& wire : graph.wires) {
        if (wire.from == fromKey && wire.to == toKey) {
            return &wire;
        }
    }
    return nullptr;
}

std::string replaced(std::string text, std::string_view from, std::string_view to)
{
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

std::string after(const Document& document, const Outcome& outcome)
{
    REQUIRE_MESSAGE(outcome.ok(), outcome.reason);
    return applied(document.source(), outcome.changes);
}

constexpr std::string_view kMachine = "machine:4:9";

}

TEST_CASE("BP-030 the S5.5 flow becomes a machine node with states, transitions and state pages")
{
    const std::string source = fixture("machine.ts");
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Graph function = document.view("fn:刷副本");
    const GraphNode* machine = titled(function, "状态机 · 挂机");
    REQUIRE(machine != nullptr);
    CHECK(machine->kind == "machine");
    CHECK(document.hasPage(kMachine));
    CHECK(document.open(std::string(machine->key)) == kMachine);

    const Graph states = document.view(kMachine);
    CHECK(states.kind == "machine");
    CHECK(states.title == "状态机 · 挂机");
    for (const std::string_view name : {"大厅", "匹配中", "对局中", "结算"}) {
        const GraphNode* state = titled(states, name);
        REQUIRE(state != nullptr);
        CHECK(state->kind == "state");
        CHECK(state->start == (name == "大厅"));
        CHECK(state->problem.empty());
    }
    CHECK(titled(states, "大厅")->subtitle == "点“开始”，最多等 10 秒");
    REQUIRE(states.wires.size() == 4);
    const GraphWire* started = wireBetween(states, "大厅", "匹配中");
    REQUIRE(started != nullptr);
    CHECK(started->type == "transition");
    CHECK(started->label == "点了开始");
    REQUIRE(wireBetween(states, "匹配中", "大厅") != nullptr);
    CHECK(wireBetween(states, "匹配中", "大厅")->label == "超时 120 秒 · 重试");
    CHECK(wireBetween(states, "匹配中", "对局中")->label == "场景 = 对局中");
    CHECK(wireBetween(states, "对局中", "结算")->label.empty());
    CHECK(document.stateNode("4:9", "匹配中") == keyOf(states, "匹配中"));
    CHECK(document.transitionWire("4:9", "大厅", "匹配中") == started->key);

    const std::string waitingPage = "state:4:9:匹配中";
    CHECK(document.open(keyOf(states, "匹配中")) == waitingPage);
    const Graph waiting = document.view(waitingPage);
    CHECK(waiting.kind == "state");
    REQUIRE(titled(waiting, "进入 匹配中") != nullptr);
    CHECK(titled(waiting, "进入 匹配中")->kind == "entry");
    const GraphNode* retry = titled(waiting, "转到 大厅");
    REQUIRE(retry != nullptr);
    CHECK(retry->kind == "return");
    CHECK(retry->badge == "超时 120 秒 · 重试");
    REQUIRE(titled(waiting, "转到 对局中") != nullptr);
    CHECK(titled(waiting, "转到 对局中")->badge == "场景 = 对局中");
    CHECK(titled(document.view("state:4:9:结算"), "结束状态机") != nullptr);

    const Json crumbs = document.crumbs(waitingPage);
    REQUIRE(crumbs.size() == 3);
    CHECK(crumbs[0]["title"] == "刷副本");
    CHECK(crumbs[1]["title"] == "状态机 · 挂机");
    CHECK(crumbs[2]["title"] == "匹配中");

    const Json details = document.details(kMachine, keyOf(states, "匹配中"));
    bool leaving = false;
    for (const Json& group : details["groups"]) {
        for (const Json& field : group["fields"]) {
            if (field["name"] == "leave") {
                leaving = field["items"].size() == 2;
            }
        }
    }
    CHECK(leaving);
}

TEST_CASE("BP-031 machine edits write the code")
{
    const std::string source = fixture("machine.ts");
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const Graph states = document.view(kMachine);

    const Outcome added = document.addState(std::string(kMachine), 640, 120);
    CHECK(after(document, added)
          == replaced(source, "      return\n    },\n", "      return\n    },\n    async 新状态(to) {\n    },\n"));
    CHECK(added.layout.pages.contains("m:" + std::string(titled(document.view("fn:刷副本"), "状态机 · 挂机")->key)));

    CHECK(after(document, document.renameState(keyOf(states, "大厅"), "大堂")) == replaced(source, "大厅", "大堂"));

    const std::string startedKey(wireBetween(states, "大厅", "匹配中")->key);
    CHECK(after(document, document.setTransitionLabel(startedKey, "开始了"))
          == replaced(source, "to.匹配中(\"点了开始\")", "to.匹配中(\"开始了\")"));
    CHECK(after(document, document.setTransitionLabel(startedKey, "")) == replaced(source, "to.匹配中(\"点了开始\")", "to.匹配中()"));

    const std::string fighting = "    async 对局中(to) {\n      await sleep(1000)\n      return to.结算()\n    },\n";
    const std::string lobby = "    // 点“开始”，最多等 10 秒\n    async 大厅(to) {";
    CHECK(after(document, document.setStart(keyOf(states, "对局中")))
          == replaced(replaced(source, fighting, ""), lobby, fighting + std::string(lobby)));

    CHECK(document.connectStates(keyOf(states, "结算"), keyOf(states, "大厅")).reason
          == "“结算”最后已经转走了，到它里面去加判断");

    CHECK(after(document, document.remove(std::string(kMachine), {keyOf(states, "匹配中")}))
          == replaced(replaced(source,
                               "    async 匹配中(to) {\n"
                               "      const ok = await vision.waitFor(res.对局中, { timeoutMs: 120000 })\n"
                               "      if (!ok) return to.大厅(\"超时 120 秒 · 重试\")\n"
                               "      return to.对局中(\"场景 = 对局中\")\n"
                               "    },\n",
                               ""),
                      "      return to.匹配中(\"点了开始\")\n", ""));

    const std::string machineKey(titled(document.view("fn:刷副本"), "状态机 · 挂机")->key);
    CHECK(after(document, document.editDetail("fn:刷副本", machineKey, "name", "刷图"))
          == replaced(source, "{ name: \"挂机\" }", "{ name: \"刷图\" }"));

    const std::string small = "await machine({\n  async a(to) {\n    await sleep(1)\n  },\n  async b(to) {\n  },\n})\n";
    const Document plain(small, environment(), {});
    REQUIRE(plain.ok());
    const Graph pair = plain.view("machine:1:7");
    CHECK(after(plain, plain.connectStates(keyOf(pair, "a"), keyOf(pair, "b")))
          == "await machine({\n  async a(to) {\n    await sleep(1)\n    return to.b()\n  },\n  async b(to) {\n  },\n})\n");
    CHECK(after(plain, plain.addState("machine:1:7", 0, 0))
          == "await machine({\n  async a(to) {\n    await sleep(1)\n  },\n  async b(to) {\n  },\n  async 新状态(to) {\n  },\n})\n");

    const Document foreign("await machine({ a: 1, async b(to) {} })\n", environment(), {});
    const Graph foreignGraph = foreign.view("");
    const GraphNode* odd = titled(foreignGraph, "状态机");
    REQUIRE(odd != nullptr);
    CHECK(odd->problem == "状态机里只能放状态");
    CHECK(foreign.view("machine:1:7").readOnly);
}
