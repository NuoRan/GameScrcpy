#include "gs/script/Report.h"

#include <doctest/doctest.h>

#include <string>

using gs::Microseconds;
using gs::script::describe;
using gs::script::kindName;
using gs::script::Report;
using gs::script::ReportKind;

TEST_CASE("a watchdog report names the module, the line and the budget")
{
    const Report report{
        .kind = ReportKind::TaskInterrupted,
        .script = 1,
        .task = 7,
        .handler = "spin",
        .location = {.module = "main.js", .line = 8, .column = 8, .function = "spin"},
        .duration = Microseconds{50'412},
        .limit = Microseconds{50'000},
    };
    CHECK(describe(report) == "main.js:8:8 in spin: Task 7 (spin) ran 50 ms without yielding and "
                              "was stopped (limit 50 ms)");
    CHECK(kindName(report.kind) == "task-interrupted");
    Report atTopLevel = report;
    atTopLevel.location = {.module = "main.js", .line = 1, .column = 1, .function = "<eval>"};
    CHECK(describe(atTopLevel).starts_with("main.js:1:1: Task 7"));
}

TEST_CASE("descriptions of failures, warnings and memory use read as sentences")
{
    CHECK(describe(Report{.kind = ReportKind::TaskFailed,
                          .task = 3,
                          .handler = "boom",
                          .message = "TypeError: cannot read property 'x' of null",
                          .location = {.module = "main.js", .line = 3}}) ==
          "main.js:3: TypeError: cannot read property 'x' of null (Task 3 (boom))");
    CHECK(describe(Report{.kind = ReportKind::ScriptFailed,
                          .script = 2,
                          .message = "SyntaxError: unexpected token in expression: ';'",
                          .location = {.module = "lib/bad.js", .line = 3, .column = 16}}) ==
          "lib/bad.js:3:16: SyntaxError: unexpected token in expression: ';'");
    CHECK(describe(Report{.kind = ReportKind::SliceSpike,
                          .task = 4,
                          .handler = "busy",
                          .duration = Microseconds{8'420},
                          .limit = Microseconds{8'000}}) ==
          "Task 4 (busy) ran 8.4 ms without yielding (warning above 8.0 ms)");
    CHECK(describe(Report{.kind = ReportKind::MemoryWarning,
                          .bytes = std::size_t{50} << 20,
                          .byteLimit = std::size_t{48} << 20}) ==
          "Scripts use 50.0 MiB of memory (warning above 48.0 MiB)");
    CHECK(describe(Report{.kind = ReportKind::MemoryLimit, .byteLimit = std::size_t{64} << 20}) ==
          "Scripts reached the 64.0 MiB memory limit and were stopped");
    CHECK(describe(Report{.kind = ReportKind::TaskCancelled, .task = 9, .handler = "hold"}) ==
          "Task 9 (hold) was cancelled");
    CHECK(describe(Report{.kind = ReportKind::Log, .message = "hello"}) == "hello");
}
