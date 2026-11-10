#include "ScriptFixture.h"

#include <doctest/doctest.h>

#include <windows.h>

#include <tlhelp32.h>

#include <charconv>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

using gs::Microseconds;
using gs::script::Limits;
using gs::script::Report;
using gs::script::ReportKind;
using gs::script::ScriptHost;
using gs::script::ScriptId;
using gs::script::TaskId;
using gs::script::test::Entry;
using gs::script::test::kindIs;
using gs::script::test::loadScript;
using gs::script::test::logged;
using gs::script::test::percentile;
using gs::script::test::Recorder;
using gs::script::test::ScriptFolder;
using gs::script::test::summary;
using gs::script::test::taskEnded;

namespace {

int threadsOfThisProcess()
{
    const DWORD process = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return -1;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    int count = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == process) {
            ++count;
        }
    }
    CloseHandle(snapshot);
    return count;
}

double millisecondsIn(const std::string& text)
{
    double value = -1.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

ScriptId loaded(ScriptHost& host, Recorder& recorder, const ScriptFolder& folder,
                std::string_view main)
{
    folder.write("main.js", main);
    ScriptId id = 0;
    const auto report = loadScript(host, recorder, folder.source(), &id);
    REQUIRE(report.has_value());
    INFO(gs::script::describe(*report));
    REQUIRE(report->kind == ReportKind::ScriptLoaded);
    return id;
}

constexpr std::string_view kTicker = R"js(export async function tick() {
    for (;;) {
        await sleep(5);
        log.info("tick");
    }
}

export function spin() {
    let turns = 0;
    while (true) {
        turns += 1;
    }
}

export function stubborn() {
    for (;;) {
        try {
            while (true) {}
        } catch (error) {
            log.info("caught");
        } finally {
            log.info("finally");
        }
    }
}

export async function lateSpin() {
    await sleep(1);
    for (;;) {}
}
)js";

}

TEST_CASE("SCR-001: a hundred sleeping tasks share the one script thread")
{
    ScriptFolder folder;
    Recorder recorder;
    ScriptHost host(recorder.sink());
    const ScriptId script = loaded(host, recorder, folder, R"js(
export async function nap() {
    log.info("napping");
    await sleep(500);
    log.info("woke");
}
)js");
    const int before = threadsOfThisProcess();
    REQUIRE(before > 0);
    std::vector<TaskId> tasks;
    for (int i = 0; i < 100; ++i) {
        tasks.push_back(host.start(script, "nap"));
    }
    REQUIRE(recorder.waitForCount(logged("napping"), 100));
    const int during = threadsOfThisProcess();
    CHECK(recorder.count(logged("woke")) == 0);
    MESSAGE("threads before ", before, ", while 100 tasks sleep ", during);
    CHECK(during <= before);
    REQUIRE(recorder.waitForCount(kindIs(ReportKind::TaskFinished), 100));
    std::set<std::thread::id> threads;
    for (const Entry& entry : recorder.entries()) {
        threads.insert(entry.thread);
    }
    CHECK(threads.size() == 1);
    CHECK(*threads.begin() != std::this_thread::get_id());
    CHECK(recorder.count(logged("woke")) == 100);
}

TEST_CASE("SCR-002: a trigger reaches the handler's first statement quickly")
{
    ScriptFolder folder;
    Recorder recorder;
    ScriptHost host(recorder.sink());
    const ScriptId script = loaded(host, recorder, folder, R"js(
export async function press({ signal }) {
    log.info("pressed");
    if (signal.aborted) {
        return;
    }
}
)js");
    std::vector<double> latencies;
    for (int i = 0; i < 320; ++i) {
        const Microseconds before = gs::SteadyClock{}.now();
        const TaskId task = host.start(script, "press");
        const auto first = recorder.waitFor(logged("pressed", task));
        REQUIRE(first.has_value());
        REQUIRE(recorder.waitFor(taskEnded(task)).has_value());
        if (i >= 20) {
            latencies.push_back(static_cast<double>((first->at - before).count()));
        }
    }
    MESSAGE("trigger to first statement: ", summary(latencies, "us"));
#if defined(NDEBUG)
    CHECK(percentile(latencies, 0.5) < 300.0);
#endif
}

TEST_CASE("SCR-003: sleep never returns early and lands close to its deadline")
{
    ScriptFolder folder;
    Recorder recorder;
    ScriptHost host(recorder.sink());
    const ScriptId script = loaded(host, recorder, folder, R"js(
export async function measure() {
    for (let i = 0; i < 60; i++) {
        const begin = time.now();
        await sleep(4);
        log.info("slept", String(time.now() - begin));
    }
}
)js");
    const TaskId task = host.start(script, "measure");
    const auto ended = recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    REQUIRE(ended->report.kind == ReportKind::TaskFinished);
    std::vector<double> errors;
    for (const Entry& entry : recorder.entries()) {
        if (entry.report.kind == ReportKind::Log && entry.report.message.starts_with("slept ")) {
            const double elapsed = millisecondsIn(entry.report.message.substr(6));
            CHECK(elapsed >= 3.999);
            errors.push_back((elapsed - 4.0) * 1000.0);
        }
    }
    REQUIRE(errors.size() == 60);
    MESSAGE("sleep(4) lateness: ", summary(errors, "us"));
    CHECK(percentile(errors, 0.5) < 2000.0);
}

TEST_CASE("SCR-003: cancelling a task rejects its pending sleep within a millisecond")
{
    ScriptFolder folder;
    Recorder recorder;
    ScriptHost host(recorder.sink());
    const ScriptId script = loaded(host, recorder, folder, R"js(
export async function hold() {
    log.info("holding");
    try {
        await sleep(60000);
    } catch (error) {
        log.info("aborted", error.name);
        throw error;
    }
}
)js");
    std::vector<double> toCatch;
    std::vector<double> toReport;
    for (int i = 0; i < 60; ++i) {
        const TaskId task = host.start(script, "hold");
        REQUIRE(recorder.waitFor(logged("holding", task)).has_value());
        const Microseconds before = gs::SteadyClock{}.now();
        host.cancel(task);
        const auto caught = recorder.waitFor(logged("aborted AbortError", task));
        const auto ended = recorder.waitFor(taskEnded(task));
        REQUIRE(caught.has_value());
        REQUIRE(ended.has_value());
        CHECK(ended->report.kind == ReportKind::TaskCancelled);
        if (i >= 10) {
            toCatch.push_back(static_cast<double>((caught->at - before).count()));
            toReport.push_back(static_cast<double>((ended->at - before).count()));
        }
    }
    MESSAGE("cancel to catch block: ", summary(toCatch, "us"));
    MESSAGE("cancel to cancelled report: ", summary(toReport, "us"));
    CHECK(percentile(toCatch, 0.5) < 1000.0);
}

TEST_CASE(
    "SCR-004: the watchdog stops a busy loop with a readable error while other tasks keep ticking")
{
    ScriptFolder folder;
    Recorder recorder;
    Limits limits;
    limits.sliceBudget = Microseconds{50'000};
    ScriptHost host(recorder.sink(), limits);
    const ScriptId script = loaded(host, recorder, folder, kTicker);
    const TaskId ticker = host.start(script, "tick");
    REQUIRE(recorder.waitForCount(logged("tick", ticker), 2));

    const TaskId spinner = host.start(script, "spin");
    const auto stopped = recorder.waitFor(taskEnded(spinner));
    REQUIRE(stopped.has_value());
    const Report& report = stopped->report;
    const std::string text = gs::script::describe(report);
    MESSAGE(text);
    CHECK(report.kind == ReportKind::TaskInterrupted);
    CHECK(report.level == gs::log::Level::Error);
    CHECK(report.handler == "spin");
    CHECK(report.location.module == "main.js");
    CHECK(report.location.function == "spin");
    CHECK(report.location.line >= 10);
    CHECK(report.location.line <= 12);
    CHECK(report.duration >= Microseconds{50'000});
    CHECK(report.limit == Microseconds{50'000});
    CHECK(text.starts_with("main.js:"));
    CHECK(text.find("limit 50 ms") != std::string::npos);
    CHECK(recorder.waitForCount(logged("tick", ticker), 3, stopped->index));

    const TaskId stubborn = host.start(script, "stubborn");
    const auto alsoStopped = recorder.waitFor(taskEnded(stubborn));
    REQUIRE(alsoStopped.has_value());
    MESSAGE(gs::script::describe(alsoStopped->report));
    CHECK(alsoStopped->report.kind == ReportKind::TaskInterrupted);
    CHECK(alsoStopped->report.location.function == "stubborn");
    CHECK(alsoStopped->report.location.line == 18);
    CHECK(recorder.count(logged("caught", stubborn)) == 0);
    CHECK(recorder.count(logged("finally", stubborn)) == 0);

    const TaskId late = host.start(script, "lateSpin");
    const auto lateStopped = recorder.waitFor(taskEnded(late));
    REQUIRE(lateStopped.has_value());
    MESSAGE(gs::script::describe(lateStopped->report));
    CHECK(lateStopped->report.kind == ReportKind::TaskInterrupted);
    CHECK(lateStopped->report.location.function == "lateSpin");
    CHECK(lateStopped->report.location.line == 29);
    CHECK(recorder.waitForCount(logged("tick", ticker), 3, lateStopped->index));

    host.cancel(ticker);
    const auto tickerEnded = recorder.waitFor(taskEnded(ticker));
    REQUIRE(tickerEnded.has_value());
    CHECK(tickerEnded->report.kind == ReportKind::TaskCancelled);
    CHECK(recorder.count(kindIs(ReportKind::SliceSpike, ticker)) == 0);
}

TEST_CASE("SCR-004: a loop at module top level fails the load with the budget")
{
    ScriptFolder folder;
    Recorder recorder;
    Limits limits;
    limits.sliceBudget = Microseconds{40'000};
    ScriptHost host(recorder.sink(), limits);
    folder.write("main.js", "export const ready = false;\nwhile (true) {}\n");
    const auto report = loadScript(host, recorder, folder.source());
    REQUIRE(report.has_value());
    MESSAGE(gs::script::describe(*report));
    CHECK(report->kind == ReportKind::ScriptFailed);
    CHECK(report->location.module == "main.js");
    CHECK(report->location.line == 2);
    CHECK(report->limit == Microseconds{40'000});
    CHECK(gs::script::describe(*report).find("limit 40 ms") != std::string::npos);
}

TEST_CASE("SCR-004: a long slice warns as a spike, steady load warns as sustained")
{
    ScriptFolder folder;
    Recorder recorder;
    Limits limits;
    limits.sliceBudget = Microseconds{1'000'000};
    limits.spikeThreshold = Microseconds{10'000};
    ScriptHost host(recorder.sink(), limits);
    const ScriptId script = loaded(host, recorder, folder, R"js(
export function busy() {
    const end = time.now() + 30;
    while (time.now() < end) {}
}
)js");
    const Report first = [&] {
        const TaskId task = host.start(script, "busy");
        return recorder.waitFor(taskEnded(task))->report;
    }();
    CHECK(first.kind == ReportKind::TaskFinished);
    const auto spike = recorder.waitFor(kindIs(ReportKind::SliceSpike, first.task));
    REQUIRE(spike.has_value());
    MESSAGE(gs::script::describe(spike->report));
    CHECK(spike->report.level == gs::log::Level::Warning);
    CHECK(spike->report.handler == "busy");
    CHECK(spike->report.duration >= Microseconds{30'000});
    CHECK(spike->report.limit == Microseconds{10'000});
    const TaskId second = host.start(script, "busy");
    REQUIRE(recorder.waitFor(taskEnded(second)).has_value());
    for (const auto& entry : recorder.entries()) {
        if (entry.report.kind == ReportKind::SliceSpike && entry.report.task == 0) {
            MESSAGE("loading the script also spiked: " << gs::script::describe(entry.report));
        }
    }
    CHECK(recorder.count([](const Report& report) {
              return report.kind == ReportKind::SliceSpike && report.task != 0;
          })
          == 1);

    ScriptFolder steadyFolder;
    Recorder steadyRecorder;
    Limits steady;
    steady.sliceBudget = Microseconds{1'000'000};
    steady.spikeThreshold = Microseconds{50'000};
    steady.sustainedWindow = Microseconds{200'000};
    steady.sustainedBusy = Microseconds{100'000};
    ScriptHost steadyHost(steadyRecorder.sink(), steady);
    const ScriptId grinder = loaded(steadyHost, steadyRecorder, steadyFolder, R"js(
export async function grind() {
    const stop = time.now() + 600;
    while (time.now() < stop) {
        const end = time.now() + 4;
        while (time.now() < end) {}
        await sleep(1);
    }
}
)js");
    const TaskId task = steadyHost.start(grinder, "grind");
    REQUIRE(steadyRecorder.waitFor(taskEnded(task)).has_value());
    const auto sustained = steadyRecorder.waitFor(kindIs(ReportKind::SustainedLoad));
    REQUIRE(sustained.has_value());
    MESSAGE(gs::script::describe(sustained->report));
    CHECK(sustained->report.duration >= Microseconds{100'000});
    CHECK(sustained->report.window == Microseconds{200'000});
    CHECK(sustained->report.task == task);
    CHECK(steadyRecorder.count(kindIs(ReportKind::SliceSpike)) == 0);
    CHECK(steadyRecorder.count(kindIs(ReportKind::TaskInterrupted)) == 0);
}

TEST_CASE("the memory warning comes before and apart from the hard limit")
{
    ScriptFolder folder;
    Recorder recorder;
    Limits limits;
    limits.memoryWarningBytes = std::size_t{4} << 20;
    limits.memoryLimitBytes = std::size_t{64} << 20;
    ScriptHost host(recorder.sink(), limits);
    const ScriptId script = loaded(host, recorder, folder, R"js(
export function hoard() {
    globalThis.kept = new Array(400000).fill(0);
    log.info("kept", String(kept.length));
}
)js");
    const TaskId task = host.start(script, "hoard");
    const auto ended = recorder.waitFor(taskEnded(task));
    REQUIRE(ended.has_value());
    CHECK(ended->report.kind == ReportKind::TaskFinished);
    const auto warning = recorder.waitFor(kindIs(ReportKind::MemoryWarning));
    REQUIRE(warning.has_value());
    MESSAGE(gs::script::describe(warning->report));
    CHECK(warning->report.bytes >= (std::size_t{4} << 20));
    CHECK(warning->report.byteLimit == (std::size_t{4} << 20));
    CHECK(recorder.count(kindIs(ReportKind::MemoryLimit)) == 0);
}

TEST_CASE("the memory hard limit stops only the runtime that reached it")
{
    ScriptFolder greedyFolder;
    ScriptFolder calmFolder;
    Recorder greedyRecorder;
    Recorder calmRecorder;
    Limits small;
    small.memoryWarningBytes = std::size_t{8} << 20;
    small.memoryLimitBytes = std::size_t{16} << 20;
    small.sliceBudget = Microseconds{10'000'000};
    ScriptHost greedy(greedyRecorder.sink(), small);
    ScriptHost calm(calmRecorder.sink());
    const ScriptId hog = loaded(greedy, greedyRecorder, greedyFolder, R"js(
export async function idle() {
    log.info("idle");
    await sleep(60000);
}

export function hog() {
    const kept = [];
    for (;;) {
        kept.push(new Array(4096).fill(1));
    }
}
)js");
    const ScriptId ticker = loaded(calm, calmRecorder, calmFolder, kTicker);
    const TaskId tick = calm.start(ticker, "tick");
    REQUIRE(calmRecorder.waitForCount(logged("tick", tick), 2));

    const TaskId idle = greedy.start(hog, "idle");
    REQUIRE(greedyRecorder.waitFor(logged("idle", idle)).has_value());
    const TaskId eater = greedy.start(hog, "hog");
    const auto limit = greedyRecorder.waitFor(kindIs(ReportKind::MemoryLimit));
    REQUIRE(limit.has_value());
    MESSAGE(gs::script::describe(limit->report));
    CHECK(limit->report.level == gs::log::Level::Error);
    CHECK(limit->report.byteLimit == (std::size_t{16} << 20));
    const auto idleEnded = greedyRecorder.waitFor(taskEnded(idle));
    const auto eaterEnded = greedyRecorder.waitFor(taskEnded(eater));
    REQUIRE(idleEnded.has_value());
    REQUIRE(eaterEnded.has_value());
    CHECK(idleEnded->report.kind == ReportKind::TaskCancelled);
    CHECK(eaterEnded->report.kind == ReportKind::TaskCancelled);

    const std::size_t ticksBefore = calmRecorder.count(logged("tick", tick));
    CHECK(calmRecorder.waitForCount(logged("tick", tick), ticksBefore + 3));
    CHECK(calmRecorder.count(kindIs(ReportKind::MemoryLimit)) == 0);

    const TaskId afterwards = greedy.start(hog, "idle");
    const auto refused = greedyRecorder.waitFor(taskEnded(afterwards));
    REQUIRE(refused.has_value());
    CHECK(refused->report.kind == ReportKind::TaskFailed);
    const ScriptId reloaded = loaded(greedy, greedyRecorder, greedyFolder,
                                     "export function ping() { log.info(\"pong\"); }\n");
    const TaskId ping = greedy.start(reloaded, "ping");
    REQUIRE(greedyRecorder.waitFor(taskEnded(ping)).has_value());
    CHECK(greedyRecorder.count(logged("pong", ping)) == 1);
    calm.cancel(tick);
    REQUIRE(calmRecorder.waitFor(taskEnded(tick)).has_value());
}

TEST_CASE("runaway recursion hits the stack limit as a catchable RangeError")
{
    ScriptFolder folder;
    Recorder recorder;
    Limits limits;
    limits.stackBytes = std::size_t{256} << 10;
    ScriptHost host(recorder.sink(), limits);
    const ScriptId script = loaded(host, recorder, folder, R"js(export function deep() {
    const down = (depth) => down(depth + 1) + 1;
    return down(0);
}
export function guarded() {
    const down = (depth) => down(depth + 1) + 1;
    try {
        down(0);
    } catch (error) {
        log.info("caught", error.name);
    }
}
)js");
    const TaskId deep = host.start(script, "deep");
    const auto ended = recorder.waitFor(taskEnded(deep));
    REQUIRE(ended.has_value());
    MESSAGE(gs::script::describe(ended->report));
    CHECK(ended->report.kind == ReportKind::TaskFailed);
    CHECK(ended->report.message.find("RangeError") != std::string::npos);
    CHECK(ended->report.location.module == "main.js");
    CHECK(ended->report.location.line == 2);
    const TaskId guarded = host.start(script, "guarded");
    const auto guardedEnded = recorder.waitFor(taskEnded(guarded));
    REQUIRE(guardedEnded.has_value());
    CHECK(guardedEnded->report.kind == ReportKind::TaskFinished);
    CHECK(recorder.count(logged("caught RangeError", guarded)) == 1);
}
