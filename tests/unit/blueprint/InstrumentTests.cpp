#include "Fixture.h"

#include <gs/blueprint/Instrument.h>
#include <gs/tsstrip/Strip.h>

#include <quickjs.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace gs::blueprint;
using namespace blueprint_test;

namespace {

class Js
{
public:
    Js() : m_runtime(JS_NewRuntime()), m_context(JS_NewContext(m_runtime)) {}
    ~Js()
    {
        JS_FreeContext(m_context);
        JS_FreeRuntime(m_runtime);
    }
    Js(const Js&) = delete;
    Js& operator=(const Js&) = delete;

    std::string run(const std::string& code, int flags, const char* name = "fixture.js")
    {
        const JSValue value = JS_Eval(m_context, code.c_str(), code.size(), name, flags);
        std::string problem;
        if (JS_IsException(value)) {
            const JSValue error = JS_GetException(m_context);
            const char* text = JS_ToCString(m_context, error);
            problem = text != nullptr ? text : "exception";
            JS_FreeCString(m_context, text);
            JS_FreeValue(m_context, error);
        }
        JS_FreeValue(m_context, value);
        return problem;
    }

    void settle()
    {
        JSContext* context = nullptr;
        while (JS_ExecutePendingJob(m_runtime, &context) > 0) {
        }
    }

    std::string text(const std::string& expression)
    {
        const JSValue value = JS_Eval(m_context, expression.c_str(), expression.size(), "read.js", JS_EVAL_TYPE_GLOBAL);
        const char* text = JS_ToCString(m_context, value);
        std::string out = text != nullptr ? text : std::string();
        JS_FreeCString(m_context, text);
        JS_FreeValue(m_context, value);
        return out;
    }

private:
    JSRuntime* m_runtime;
    JSContext* m_context;
};

std::size_t lineCount(std::string_view text)
{
    return static_cast<std::size_t>(std::ranges::count(text, '\n')) + 1;
}

std::vector<std::filesystem::path> sources()
{
    std::vector<std::filesystem::path> found;
    for (const auto& root : {std::filesystem::path(GS_BLUEPRINT_DATA_DIR), std::filesystem::path(GS_EXAMPLES_DIR),
                             std::filesystem::path(GS_SCRIPTS_DATA_DIR)}) {
        if (!std::filesystem::exists(root)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ts") {
                found.push_back(entry.path());
            }
        }
    }
    return found;
}

constexpr std::string_view kPrelude = R"(const calls = [];
globalThis.__gs = {
  probe(at, values) { calls.push("probe " + at + " " + Object.keys(values).sort().join(",")); return at === "11:10"; },
  pause(at) { calls.push("pause " + at); return Promise.resolve(); },
  enter(at) { calls.push("enter " + at); return at; },
  exit(frame) { calls.push("exit " + frame); },
};
globalThis.keys = { F: { onTap(h) { h({}); } }, G: { onTap(h) { h({}); } } };
globalThis.log = { info() {} };
globalThis.sleep = () => Promise.resolve();
)";

constexpr std::string_view kRun = R"(function pick(n) {
  let total = 0
  if (n > 1) total = 10
  else total = 1
  while (n > 0) n--
  return total
}
keys.F.onTap(() => log.info(pick(2)))
keys.G.onTap(async (e) => {
  const v = pick(0)
  if (v) await sleep(1)
  for (const x of [1, 2]) log.info(x)
})
)";

}

TEST_CASE("BP-040 instrumented fixtures keep their lines and still compile")
{
    std::size_t checked = 0;
    for (const std::filesystem::path& path : sources()) {
        CAPTURE(path.string());
        const std::string source = lf(readFile(path));
        const Instrumented result = instrument(source, environment(path.filename().string()));
        CHECK(lineCount(result.javascript) == lineCount(source));
        const std::string plain = gs::tsstrip::strip(source, gs::tsstrip::Offsets::Bytes).javascript;
        Js js;
        if (!js.run(plain, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY).empty()) {
            continue;
        }
        Js again;
        CHECK(again.run(result.javascript, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY) == "");
        CHECK(result.problems.empty());
        ++checked;
    }
    CHECK(checked >= 6);
}

TEST_CASE("BP-041 probes, pauses and frames run in order through branches, loops and arrows")
{
    const Instrumented result = instrument(kRun, environment());
    REQUIRE(result.problems.empty());
    CHECK(lineCount(result.javascript) == lineCount(kRun));
    Js js;
    REQUIRE(js.run(std::string(kPrelude), JS_EVAL_TYPE_GLOBAL, "prelude.js") == "");
    REQUIRE(js.run(result.javascript, JS_EVAL_TYPE_GLOBAL) == "");
    js.settle();
    CHECK(js.text("calls.join('\\n')")
          == "probe 8:1 \n"
             "enter 8:14\n"
             "probe 8:20 \n"
             "enter 1:1\n"
             "probe 2:3 n\n"
             "probe 3:3 n,total\n"
             "probe 3:14 n,total\n"
             "probe 5:3 n,total\n"
             "probe 5:17 n,total\n"
             "probe 5:17 n,total\n"
             "probe 6:3 n,total\n"
             "exit 1:1\n"
             "exit 8:14\n"
             "probe 9:1 \n"
             "enter 9:14\n"
             "probe 10:3 e\n"
             "enter 1:1\n"
             "probe 2:3 n\n"
             "probe 3:3 n,total\n"
             "probe 4:8 n,total\n"
             "probe 5:3 n,total\n"
             "probe 6:3 n,total\n"
             "exit 1:1\n"
             "probe 11:3 e,v\n"
             "probe 11:10 e,v\n"
             "pause 11:10\n"
             "probe 12:3 e,v\n"
             "probe 12:27 e,v,x\n"
             "probe 12:27 e,v,x\n"
             "exit 9:14");
    const auto has = [&result](std::string_view position, std::string_view kind, bool async) {
        return std::ranges::any_of(result.probes, [&](const Probe& probe) {
            return probe.position == position && probe.kind == kind && probe.async == async;
        });
    };
    CHECK(has("1:1", "entry", false));
    CHECK(has("11:10", "statement", true));
    CHECK(has("3:14", "statement", false));
}

TEST_CASE("BP-042 breakpoints map to nodes and need an async body")
{
    const std::string source(kRun);
    const Document document(source, environment(), {});
    REQUIRE(document.ok());
    const std::string waitKey = document.nodeAt("11:10");
    REQUIRE_FALSE(waitKey.empty());
    const std::string setKey = document.nodeAt("3:14");
    REQUIRE_FALSE(setKey.empty());
    Layout layout = document.layout();
    layout.breakpoints.push_back({waitKey, true});
    layout.breakpoints.push_back({setKey, false});
    const Document marked(source, environment(), layout);
    const Json positions = marked.breakpointPositions();
    REQUIRE(positions.size() == 1);
    CHECK(positions[0]["position"] == "11:10");
    CHECK(positions[0]["enabled"] == true);
    const Json graph = marked.graph("fn:pick");
    bool invalid = false;
    for (const Json& node : graph["nodes"]) {
        if (node["key"] == setKey) {
            invalid = node["breakpoint"] == "invalid" && node["breakpointReason"] == "只能停在 async 函数里";
        }
    }
    CHECK(invalid);
}
