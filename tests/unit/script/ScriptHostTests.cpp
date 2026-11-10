#include "ScriptFixture.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using gs::script::Limits;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::TaskId;
using gs::script::test::kindIs;
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::taskEnded;

namespace {

struct Fixture {
    ScriptFolder folder;
    Recorder recorder;
    std::unique_ptr<ScriptHost> host;

    explicit Fixture(Limits limits = {})
        : host(std::make_unique<ScriptHost>(recorder.sink(), limits))
    {}

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

    Report failedLoad()
    {
        const auto report = loadScript(*host, recorder, folder.source());
        REQUIRE(report.has_value());
        REQUIRE(report->kind == ReportKind::ScriptFailed);
        return *report;
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

TEST_CASE("a script host runs an exported handler of an ES module as a task")
{
    Fixture fixture;
    REQUIRE(fixture.host->started());
    const ScriptId script = fixture.load(R"js(
export function greet() {
    log.info("hello", 42, true);
    log.warn("careful");
    log.error("broken", null);
}
)js");
    const Report ended = fixture.run(script, "greet");
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(ended.handler == "greet");
    const auto hello = fixture.recorder.waitFor(logged("hello 42 true", ended.task));
    REQUIRE(hello.has_value());
    CHECK(hello->report.level == gs::log::Level::Info);
    CHECK(hello->report.script == script);
    CHECK(hello->report.handler == "greet");
    CHECK(fixture.recorder.waitFor(logged("careful"))->report.level == gs::log::Level::Warning);
    CHECK(fixture.recorder.waitFor(logged("broken null"))->report.level == gs::log::Level::Error);
    CHECK(fixture.host->problem().empty());
}

TEST_CASE("an async handler gets an abort signal and ends when its promise settles")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function check({ signal }) {
    const listens = typeof signal.addEventListener;
    log.info(String(signal instanceof AbortSignal), String(signal.aborted), listens);
    await sleep(1);
    log.info("after");
}
)js");
    const Report ended = fixture.run(script, "check");
    CHECK(ended.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("true false function", ended.task)) == 1);
    CHECK(fixture.recorder.count(logged("after", ended.task)) == 1);
}

TEST_CASE("modules import each other with relative paths, also at run time")
{
    Fixture fixture;
    fixture.folder.write("lib/math.js", R"js(
import { base } from "../shared/base.js";
export const double = (value) => value * base;
)js");
    fixture.folder.write("shared/base.js", "export const base = 2;\n");
    fixture.folder.write("later.mjs", "export const value = \"loaded later\";\n");
    const ScriptId script = fixture.load(R"js(
import { double } from "./lib/math.js";
export function compute() {
    log.info(String(double(21)));
}
export async function late() {
    const { value } = await import("./later.mjs");
    log.info(value);
}
)js");
    CHECK(fixture.run(script, "compute").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("42")) == 1);
    CHECK(fixture.run(script, "late").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("loaded later")) == 1);
}

TEST_CASE("component files load after the main page, each in its own module, sharing only globals (ADR-0091)")
{
    Fixture fixture;
    fixture.folder.write("main.js", R"js(
const secret = "main";
globalThis.order = ["main"];
export function report() {
    log.info(globalThis.order.join(","), typeof fromB, secret);
}
)js");
    fixture.folder.write("controls/a.ts", "const secret: string = \"a\";\nglobalThis.order.push(\"a:\" + secret);\n");
    fixture.folder.write("controls/b.js", "const fromB = 1;\nglobalThis.order.push(\"b\");\n");
    fixture.folder.write("controls/types.d.ts", "declare const shared: number;\n");
    fixture.folder.write("controls/notes.txt", "not a module\n");
    gs::script::ScriptSource source = fixture.folder.source();
    source.modules = gs::script::controlModules(source.folder);
    CHECK(source.modules == std::vector<std::string>{"controls/a.ts", "controls/b.js"});
    ScriptId script = 0;
    const auto loaded = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    REQUIRE(loaded->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.run(script, "report").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("main,a:a,b undefined main")) == 1);
}

TEST_CASE("component files run without a main page, and a broken one fails the load with its own name (ADR-0091)")
{
    Fixture fixture;
    fixture.folder.write("controls/only.js", "log.info(\"only\");\n");
    gs::script::ScriptSource alone = fixture.folder.source("logic.ts");
    alone.modules = {"controls/only.js"};
    const auto loaded = loadScript(*fixture.host, fixture.recorder, alone);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    CHECK(loaded->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.recorder.waitFor(logged("only")).has_value());

    fixture.folder.write("main.js", "export function run() {}\n");
    fixture.folder.write("controls/fire.js", "const x = ;\n");
    gs::script::ScriptSource broken = fixture.folder.source();
    broken.modules = {"controls/fire.js"};
    const auto failed = loadScript(*fixture.host, fixture.recorder, broken);
    REQUIRE(failed.has_value());
    CHECK(failed->kind == ReportKind::ScriptFailed);
    CHECK(failed->location.module == "controls/fire.js");

    gs::script::ScriptSource outside = fixture.folder.source();
    outside.modules = {"../elsewhere.js"};
    const auto refused = loadScript(*fixture.host, fixture.recorder, outside);
    REQUIRE(refused.has_value());
    CHECK(refused->kind == ReportKind::ScriptFailed);
}

TEST_CASE("the main page and a component file import the same library.ts module (ADR-0091)")
{
    Fixture fixture;
    fixture.folder.write("library.ts",
                         "let calls: number = 0;\nexport function count(from: string): number {\n    calls += 1;\n"
                         "    return calls;\n}\n");
    fixture.folder.write("main.js", R"js(
import { count } from "./library.ts";
export function report() {
    log.info("main " + count("main"));
}
)js");
    fixture.folder.write("controls/fire.ts",
                         "import { count } from \"../library.ts\";\nlog.info(\"fire \" + count(\"fire\"));\n");
    gs::script::ScriptSource source = fixture.folder.source();
    source.modules = gs::script::controlModules(source.folder);
    REQUIRE(source.modules == std::vector<std::string>{"controls/fire.ts"});
    ScriptId script = 0;
    const auto loaded = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    REQUIRE(loaded->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.recorder.waitFor(logged("fire 1")).has_value());
    CHECK(fixture.run(script, "report").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("main 2")) == 1);
}

TEST_CASE("a syntax error in an imported module names that module and its line")
{
    Fixture fixture;
    fixture.folder.write("main.js", "import \"./lib/bad.js\";\nexport function run() {}\n");
    fixture.folder.write(
        "lib/bad.js", "export const fine = 1;\nexport function broken() {\n    return 1 +;\n}\n");
    const Report failed = fixture.failedLoad();
    INFO(gs::script::describe(failed));
    CHECK(failed.location.module == "lib/bad.js");
    CHECK(failed.location.line == 3);
    CHECK(failed.message.find("SyntaxError") != std::string::npos);
}

TEST_CASE("an import that cannot be found names the importing module")
{
    Fixture fixture;
    fixture.folder.write("main.js",
                         "import { x } from \"./missing.js\";\nexport function run() {}\n");
    const Report failed = fixture.failedLoad();
    INFO(gs::script::describe(failed));
    CHECK(failed.location.module == "main.js");
    CHECK(failed.message.find("./missing.js") != std::string::npos);
    CHECK(failed.message.find("cannot be found") != std::string::npos);
}

TEST_CASE("imports must be relative, stay in the script folder and name source modules")
{
    Fixture fixture;
    const struct {
        const char* source;
        const char* reason;
    } cases[] = {
        {"import \"lodash\";\n", "only relative imports"},
        {"import \"../outside.js\";\n", "leaves the script folder"},
        {"import \"./data.json\";\n", ".mjs, .ts or .mts"},
        {"import data from \"./data.js\" with { type: \"json\" };\n",
         "Import attributes are not supported"},
    };
    fixture.folder.write("data.js", "export default 1;\n");
    for (const auto& entry : cases) {
        CAPTURE(entry.source);
        fixture.folder.write("main.js", entry.source);
        const Report failed = fixture.failedLoad();
        INFO(gs::script::describe(failed));
        CHECK(failed.message.find(entry.reason) != std::string::npos);
    }
    const auto outside =
        loadScript(*fixture.host, fixture.recorder, fixture.folder.source("../main.js"));
    REQUIRE(outside.has_value());
    CHECK(outside->kind == ReportKind::ScriptFailed);
    CHECK(outside->message.find("entry module") != std::string::npos);
    const auto missing =
        loadScript(*fixture.host, fixture.recorder, fixture.folder.source("absent.js"));
    REQUIRE(missing.has_value());
    CHECK(missing->kind == ReportKind::ScriptFailed);
    CHECK(missing->message.find("cannot be found") != std::string::npos);
}

TEST_CASE("TypeScript modules are erased to JavaScript before they run and keep their lines")
{
    Fixture fixture;
    fixture.folder.write("lib/math.ts", R"ts(export interface Pair { a: number; b: number }
export const add = ({ a, b }: Pair): number => a + b;
)ts");
    fixture.folder.write("main.ts", R"ts(import { add, type Pair } from "./lib/math.ts";
const pair: Pair = { a: 20, b: 22 };
export function compute(): void {
    log.info(String(add(pair) as number));
}
export function fail(): never {
    throw new Error("typed failure");
}
)ts");
    ScriptId script = 0;
    const auto loaded =
        loadScript(*fixture.host, fixture.recorder, fixture.folder.source("main.ts"), &script);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    REQUIRE(loaded->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.run(script, "compute").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("42")) == 1);
    const Report failed = fixture.run(script, "fail");
    INFO(gs::script::describe(failed));
    CHECK(failed.kind == ReportKind::TaskFailed);
    CHECK(failed.location.module == "main.ts");
    CHECK(failed.location.line == 7);
}

TEST_CASE("TypeScript that cannot be erased fails the load with its code, module, line and column")
{
    Fixture fixture;
    fixture.folder.write("main.js", "import \"./lib/mode.ts\";\nexport function run() {}\n");
    fixture.folder.write("lib/mode.ts", "export const a = 1;\nexport enum Mode { Fast, Slow }\n");
    const Report imported = fixture.failedLoad();
    INFO(gs::script::describe(imported));
    CHECK(imported.location.module == "lib/mode.ts");
    CHECK(imported.location.line == 2);
    CHECK(imported.location.column == 8);
    CHECK(imported.message.find("GS0001") != std::string::npos);
    CHECK(imported.message.find("SyntaxError") != std::string::npos);

    fixture.folder.write("main.ts",
                         "let x: number = 1;\nnamespace App { export const y = 2; }\nexport function run() {}\n");
    const auto entry = loadScript(*fixture.host, fixture.recorder, fixture.folder.source("main.ts"));
    REQUIRE(entry.has_value());
    INFO(gs::script::describe(*entry));
    CHECK(entry->kind == ReportKind::ScriptFailed);
    CHECK(entry->location.module == "main.ts");
    CHECK(entry->location.line == 2);
    CHECK(entry->location.column == 1);
    CHECK(entry->message.find("GS0003") != std::string::npos);
}

TEST_CASE("an error at module top level fails the load with its line")
{
    Fixture fixture;
    fixture.folder.write("main.js", "export const before = 1;\nthrow new Error(\"top level\");\n");
    const Report failed = fixture.failedLoad();
    INFO(gs::script::describe(failed));
    CHECK(failed.location.module == "main.js");
    CHECK(failed.location.line == 2);
    CHECK(failed.message == "Error: top level");
}

TEST_CASE("an uncaught error in a task reports its module, line and message")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(export function boom() {
    const value = null;
    return value.missing;
}
export async function later() {
    await sleep(1);
    throw new RangeError("late");
}
)js");
    const Report sync = fixture.run(script, "boom");
    INFO(gs::script::describe(sync));
    CHECK(sync.kind == ReportKind::TaskFailed);
    CHECK(sync.location.module == "main.js");
    CHECK(sync.location.line == 3);
    CHECK(sync.message.find("TypeError") != std::string::npos);
    const Report async = fixture.run(script, "later");
    INFO(gs::script::describe(async));
    CHECK(async.kind == ReportKind::TaskFailed);
    CHECK(async.location.line == 7);
    CHECK(async.message == "RangeError: late");
}

TEST_CASE("a handler that is not exported or a script that is not loaded fails the task")
{
    Fixture fixture;
    const ScriptId script = fixture.load("export const value = 1;\n");
    const Report missing = fixture.run(script, "nope");
    CHECK(missing.kind == ReportKind::TaskFailed);
    CHECK(missing.message.find("does not export a function named 'nope'") != std::string::npos);
    CHECK(fixture.run(script, "value").kind == ReportKind::TaskFailed);
    const Report unknown = fixture.run(script + 100, "anything");
    CHECK(unknown.kind == ReportKind::TaskFailed);
    CHECK(unknown.message.find("is not loaded") != std::string::npos);
    CHECK(fixture.recorder.count([](const Report& report) {
        return report.message.find("Illegal invocation") != std::string::npos;
    }) == 0);
    CHECK(fixture.recorder.count(kindIs(ReportKind::TaskFailed, missing.task)) == 1);
}

TEST_CASE("tasks started while a module still loads run once it has loaded, or are cancelled")
{
    Fixture fixture;
    fixture.folder.write("main.js", R"js(
await sleep(30);
export function ready() {
    log.info("ready");
}
)js");
    const ScriptId script = fixture.host->load(fixture.folder.source());
    const TaskId first = fixture.host->start(script, "ready");
    const TaskId second = fixture.host->start(script, "ready");
    fixture.host->cancel(second);
    const auto ended = fixture.recorder.waitFor(taskEnded(first));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    const auto loaded = fixture.recorder.waitFor(kindIs(ReportKind::ScriptLoaded));
    const auto log = fixture.recorder.waitFor(logged("ready", first));
    REQUIRE(loaded.has_value());
    REQUIRE(log.has_value());
    CHECK(loaded->index < log->index);
    const auto cancelled = fixture.recorder.waitFor(taskEnded(second));
    REQUIRE(cancelled.has_value());
    CHECK(cancelled->report.kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(logged("ready", second)) == 0);
}

TEST_CASE("eval and the Function constructors are not available to scripts")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export function probe() {
    const attempts = [
        () => eval("1"),
        () => new Function("return 1"),
        () => Function("return 1"),
        () => (async function () {}).constructor("return 1"),
        () => (function* () {}).constructor("yield 1"),
        () => (async function* () {}).constructor("yield 1"),
        () => new AbortSignal(),
    ];
    const results = attempts.map((attempt) => {
        try {
            attempt();
            return "allowed";
        } catch (error) {
            return error.name;
        }
    });
    log.info(results.join(" "), typeof globalThis.eval, String((() => {}) instanceof Function));
}
)js");
    CHECK(fixture.run(script, "probe").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("ReferenceError EvalError EvalError EvalError EvalError "
                                        "EvalError TypeError undefined true")) == 1);
}

TEST_CASE("nextFrame resolves on the host's frame call, only for waits made before it")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function frames() {
    log.info("waiting");
    const at = await nextFrame();
    log.info("first", typeof at);
    await nextFrame();
    log.info("second");
}
export function ping() {}
)js");
    const TaskId task = fixture.host->start(script, "frames");
    REQUIRE(fixture.recorder.waitFor(logged("waiting", task)).has_value());
    fixture.host->frame();
    REQUIRE(fixture.recorder.waitFor(logged("first number", task)).has_value());
    CHECK(fixture.run(script, "ping").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("second", task)) == 0);
    fixture.host->frame();
    REQUIRE(fixture.recorder.waitFor(logged("second", task)).has_value());
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
}

TEST_CASE("time.now counts milliseconds across a sleep")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function clock() {
    const before = time.now();
    await sleep(10);
    const after = time.now();
    log.info(typeof before, String(after - before >= 10), String(Object.isFrozen(time)));
}
export async function invalid() {
    await sleep(-1);
}
)js");
    CHECK(fixture.run(script, "clock").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(logged("number true true")) == 1);
    const Report invalid = fixture.run(script, "invalid");
    CHECK(invalid.kind == ReportKind::TaskFailed);
    CHECK(invalid.message.find("RangeError") != std::string::npos);
}

TEST_CASE("an unhandled rejection is reported with its location, a late handled one is not")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(export function leak() {
    Promise.reject(new Error("lost"));
}
export async function handled() {
    const late = Promise.reject(new Error("caught"));
    await null;
    late.catch(() => {});
}
export function ping() {}
)js");
    const TaskId leak = fixture.host->start(script, "leak");
    const auto rejection = fixture.recorder.waitFor(kindIs(ReportKind::UnhandledRejection, leak));
    REQUIRE(rejection.has_value());
    CHECK(rejection->report.message == "Error: lost");
    CHECK(rejection->report.location.module == "main.js");
    CHECK(rejection->report.location.line == 2);
    const Report handled = fixture.run(script, "handled");
    CHECK(handled.kind == ReportKind::TaskFinished);
    CHECK(fixture.run(script, "ping").kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count(kindIs(ReportKind::UnhandledRejection, handled.task)) == 0);
}

TEST_CASE("cancelling a task fires its abort signal and rejects its waits with an AbortError")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function hold({ signal }) {
    signal.addEventListener("abort", () => log.info("listener", signal.aborted));
    signal.onabort = () => log.info("onabort");
    log.info("holding");
    try {
        await sleep(60000);
    } catch (error) {
        log.info("caught", error.name, signal.reason === error);
        try {
            signal.throwIfAborted();
        } catch (again) {
            log.info("throwIfAborted", again.name);
        }
        try {
            await sleep(1);
        } catch (late) {
            log.info("late", late.name);
        }
        throw error;
    }
}
)js");
    const TaskId task = fixture.host->start(script, "hold");
    REQUIRE(fixture.recorder.waitFor(logged("holding", task)).has_value());
    fixture.host->cancel(task);
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(logged("listener true", task)) == 1);
    CHECK(fixture.recorder.count(logged("onabort", task)) == 1);
    CHECK(fixture.recorder.count(logged("caught AbortError true", task)) == 1);
    CHECK(fixture.recorder.count(logged("throwIfAborted AbortError", task)) == 1);
    CHECK(fixture.recorder.count(logged("late AbortError", task)) == 1);
    fixture.host->cancel(task);
    fixture.host->cancel(task + 1000);
    const TaskId next = fixture.host->start(script, "hold");
    REQUIRE(fixture.recorder.waitFor(logged("holding", next)).has_value());
    fixture.host->cancel(next);
    const auto nextEnded = fixture.recorder.waitFor(taskEnded(next));
    REQUIRE(nextEnded.has_value());
    CHECK(nextEnded->report.kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(taskEnded(task)) == 1);
}

TEST_CASE("unloading a script and destroying the host cancel running tasks and run their cleanup")
{
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export async function hold() {
    log.info("holding");
    try {
        await sleep(60000);
    } finally {
        log.info("cleanup");
    }
}
)js");
    const TaskId first = fixture.host->start(script, "hold");
    REQUIRE(fixture.recorder.waitFor(logged("holding", first)).has_value());
    fixture.host->unload(script);
    const auto unloaded = fixture.recorder.waitFor(taskEnded(first));
    REQUIRE(unloaded.has_value());
    CHECK(unloaded->report.kind == ReportKind::TaskCancelled);
    CHECK(fixture.recorder.count(logged("cleanup", first)) == 1);
    CHECK(fixture.run(script, "hold").kind == ReportKind::TaskFailed);

    const ScriptId again = fixture.load(R"js(
export async function hold() {
    log.info("holding");
    try {
        await sleep(60000);
    } finally {
        log.info("cleanup");
    }
}
)js");
    const TaskId second = fixture.host->start(again, "hold");
    REQUIRE(fixture.recorder.waitFor(logged("holding", second)).has_value());
    fixture.host.reset();
    CHECK(fixture.recorder.count(logged("cleanup", second)) == 1);
    CHECK(fixture.recorder.count(gs::script::test::kindIs(ReportKind::TaskCancelled, second)) == 1);
}

TEST_CASE("a task asked for starts on the script thread well within 0.3 ms (PLAN 0.4)")
{
#ifdef NDEBUG
    constexpr bool optimised = true;
#else
    constexpr bool optimised = false;
#endif
    Fixture fixture;
    const ScriptId script = fixture.load(R"js(
export function go() {
    log.info("go");
}
)js");
    std::vector<double> waitedUs;
    for (int i = 0; i < 300; ++i) {
        const std::size_t from = fixture.recorder.size();
        const gs::Microseconds asked = gs::SteadyClock{}.now();
        const TaskId task = fixture.host->start(script, "go");
        const auto started = fixture.recorder.waitFor(logged("go", task), from);
        REQUIRE(started.has_value());
        REQUIRE(fixture.recorder.waitFor(taskEnded(task), from).has_value());
        waitedUs.push_back(static_cast<double>((started->at - asked).count()));
    }
    std::ranges::sort(waitedUs);
    const double median = waitedUs[waitedUs.size() / 2];
    const double p95 = waitedUs[waitedUs.size() * 95 / 100];
    MESSAGE(std::string(optimised ? "optimised build: " : "debug build, not checked: ")
            << "asked to first statement p50 " << median << " us, p95 " << p95 << " us, slowest " << waitedUs.back()
            << " us");
    if (optimised) {
        CHECK(median < 300.0);
    }
}

TEST_CASE("ADR-0091: a debugged script stops one task at a breakpoint, shows its values, steps and reports the places it passed")
{
    Fixture fixture;
    fixture.folder.write("main.ts", "export default async function () {}\n");
    gs::script::ScriptSource source = fixture.folder.source("main.ts");
    source.transform = [](const std::string& module, const std::string&) -> std::optional<std::string> {
        if (module != "main.ts") {
            return std::nullopt;
        }
        return std::string(R"js(const __gs=__gsFor("main.ts");
export default async function () {
    const __gsf = __gs.enter("1:1");
    try {
        let a = 1;
        if (__gs.probe("5:9", { a })) await __gs.pause("5:9");
        a = 2;
        if (__gs.probe("7:9", { a })) await __gs.pause("7:9");
        log.info("done " + a);
    } finally {
        __gs.exit(__gsf);
    }
}
)js");
    };
    ScriptId script = 0;
    const auto loaded = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    REQUIRE(loaded->kind == ReportKind::ScriptLoaded);
    fixture.host->setBreakpoints({"main.ts#5:9"});
    const TaskId task = fixture.host->start(script, "default");
    const auto first = fixture.recorder.waitFor(kindIs(ReportKind::DebugPaused, task));
    REQUIRE(first.has_value());
    CHECK(first->report.message.find(R"("position":"5:9")") != std::string::npos);
    CHECK(first->report.message.find(R"("values":{"a":"1"})") != std::string::npos);
    CHECK(first->report.message.find(R"("stack":["main.ts#1:1"])") != std::string::npos);
    const std::size_t pauseAt = first->report.message.find(R"("pause":)") + 8;
    const std::uint64_t pause = std::stoull(first->report.message.substr(pauseAt));
    CHECK(fixture.recorder.count(logged("done 2", task)) == 0);

    fixture.host->resume(pause, gs::script::StepMode::Over);
    const auto second = fixture.recorder.waitFor(
        [task](const Report& report) {
            return report.kind == ReportKind::DebugPaused && report.task == task &&
                   report.message.find(R"("position":"7:9")") != std::string::npos;
        },
        first->index + 1);
    REQUIRE(second.has_value());
    CHECK(second->report.message.find(R"("values":{"a":"2"})") != std::string::npos);
    CHECK(fixture.recorder.count(kindIs(ReportKind::DebugResumed, task), first->index) >= 1);
    const std::size_t nextAt = second->report.message.find(R"("pause":)") + 8;
    fixture.host->resume(std::stoull(second->report.message.substr(nextAt)), gs::script::StepMode::Continue);

    REQUIRE(fixture.recorder.waitFor(logged("done 2", task)).has_value());
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    const auto hits = fixture.recorder.waitFor(kindIs(ReportKind::DebugHits));
    REQUIRE(hits.has_value());
    CHECK(hits->report.message.find(R"("file":"main.ts","position":"5:9")") != std::string::npos);
}

TEST_CASE("ADR-0091: making the debugged copy of the modules is not counted against the time a script may run without yielding")
{
    Limits limits;
    limits.sliceBudget = gs::Microseconds{100'000};
    limits.spikeThreshold = gs::Microseconds{50'000};
    Fixture fixture(limits);
    fixture.folder.write("main.ts", "import { value } from \"./lib.ts\";\nlog.info(\"loaded \" + value);\n"
                                    "export default async function () {}\n");
    fixture.folder.write("lib.ts", "export const value: number = 7;\n");
    gs::script::ScriptSource source = fixture.folder.source("main.ts");
    source.transform = [](const std::string& module, const std::string&) -> std::optional<std::string> {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (module == "main.ts") {
            return std::string("import { value } from \"./lib.ts\";\nlog.info(\"loaded \" + value);\n"
                               "export default async function () {}\n");
        }
        return std::string("export const value = 7;\n");
    };
    ScriptId script = 0;
    const auto loaded = loadScript(*fixture.host, fixture.recorder, source, &script);
    REQUIRE(loaded.has_value());
    INFO(gs::script::describe(*loaded));
    REQUIRE(loaded->kind == ReportKind::ScriptLoaded);
    CHECK(fixture.recorder.waitFor(logged("loaded 7")).has_value());
    const TaskId task = fixture.host->start(script, "default");
    const auto ended = fixture.recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    CHECK(fixture.recorder.count([script](const Report& report) {
              return report.script == script && report.task == 0 &&
                     (report.kind == ReportKind::SliceSpike || report.kind == ReportKind::ScriptFailed);
          }) == 0);
}
