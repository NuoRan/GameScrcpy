#include "gs/script/Report.h"

#include <format>

namespace gs::script {

namespace {

std::string milliseconds(Microseconds value)
{
    const double ms = static_cast<double>(value.count()) / 1000.0;
    return ms >= 10.0 ? std::format("{:.0f} ms", ms) : std::format("{:.1f} ms", ms);
}

std::string mebibytes(std::size_t bytes)
{
    return std::format("{:.1f} MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

std::string where(const SourceLocation& location)
{
    if (location.module.empty()) {
        return {};
    }
    std::string text = location.module;
    if (location.line > 0) {
        text += std::format(":{}", location.line);
        if (location.column > 0) {
            text += std::format(":{}", location.column);
        }
    }
    if (!location.function.empty() && location.function.front() != '<') {
        text += " in " + location.function;
    }
    return text + ": ";
}

std::string taskOf(const Report& report)
{
    if (report.task == 0) {
        return std::format("Script {}", report.script);
    }
    if (report.handler.empty()) {
        return std::format("Task {}", report.task);
    }
    return std::format("Task {} ({})", report.task, report.handler);
}

}

std::string_view kindName(ReportKind kind) noexcept
{
    switch (kind) {
    case ReportKind::Log:
        return "log";
    case ReportKind::ScriptLoaded:
        return "script-loaded";
    case ReportKind::ScriptFailed:
        return "script-failed";
    case ReportKind::TaskFinished:
        return "task-finished";
    case ReportKind::TaskCancelled:
        return "task-cancelled";
    case ReportKind::TaskFailed:
        return "task-failed";
    case ReportKind::TaskInterrupted:
        return "task-interrupted";
    case ReportKind::UnhandledRejection:
        return "unhandled-rejection";
    case ReportKind::SliceSpike:
        return "slice-spike";
    case ReportKind::SustainedLoad:
        return "sustained-load";
    case ReportKind::MemoryWarning:
        return "memory-warning";
    case ReportKind::MemoryLimit:
        return "memory-limit";
    case ReportKind::DebugHits:
        return "debug-hits";
    case ReportKind::DebugPaused:
        return "debug-paused";
    case ReportKind::DebugResumed:
        return "debug-resumed";
    }
    return "unknown";
}

std::string describe(const Report& report)
{
    const std::string at = where(report.location);
    switch (report.kind) {
    case ReportKind::Log:
        return report.message;
    case ReportKind::ScriptLoaded:
        return std::format("Loaded {}", report.location.module);
    case ReportKind::ScriptFailed:
        if (report.limit.count() > 0) {
            return std::format("{}The script ran {} without yielding and was stopped (limit {})",
                               at, milliseconds(report.duration), milliseconds(report.limit));
        }
        return at + report.message;
    case ReportKind::TaskFinished:
        return taskOf(report) + " finished";
    case ReportKind::TaskCancelled:
        return taskOf(report) + " was cancelled";
    case ReportKind::TaskFailed:
        return std::format("{}{} ({})", at, report.message, taskOf(report));
    case ReportKind::TaskInterrupted:
        return std::format("{}{} ran {} without yielding and was stopped (limit {})", at,
                           taskOf(report), milliseconds(report.duration),
                           milliseconds(report.limit));
    case ReportKind::UnhandledRejection:
        return std::format("{}Unhandled rejection: {} ({})", at, report.message, taskOf(report));
    case ReportKind::SliceSpike:
        return std::format("{} ran {} without yielding (warning above {})", taskOf(report),
                           milliseconds(report.duration), milliseconds(report.limit));
    case ReportKind::SustainedLoad:
        return std::format("Scripts were busy {} of the last {} (warning above {}); latest: {}",
                           milliseconds(report.duration), milliseconds(report.window),
                           milliseconds(report.limit), taskOf(report));
    case ReportKind::MemoryWarning:
        return std::format("Scripts use {} of memory (warning above {})", mebibytes(report.bytes),
                           mebibytes(report.byteLimit));
    case ReportKind::MemoryLimit:
        return std::format("Scripts reached the {} memory limit and were stopped",
                           mebibytes(report.byteLimit));
    case ReportKind::DebugHits:
    case ReportKind::DebugPaused:
    case ReportKind::DebugResumed:
        return report.message;
    }
    return report.message;
}

}
