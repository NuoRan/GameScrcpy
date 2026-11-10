#pragma once

#include <gs/base/Clock.h>
#include <gs/base/Log.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gs::script {

using ScriptId = std::uint32_t;
using TaskId = std::uint64_t;

enum class ReportKind : std::uint8_t {
    Log,
    ScriptLoaded,
    ScriptFailed,
    TaskFinished,
    TaskCancelled,
    TaskFailed,
    TaskInterrupted,
    UnhandledRejection,
    SliceSpike,
    SustainedLoad,
    MemoryWarning,
    MemoryLimit,
    DebugHits,
    DebugPaused,
    DebugResumed,
};

enum class StepMode : std::uint8_t { Continue, Over, Into, Out };

struct SourceLocation {
    std::string module;
    int line = 0;
    int column = 0;
    std::string function;

    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
};

struct Report {
    ReportKind kind = ReportKind::Log;
    log::Level level = log::Level::Info;
    ScriptId script = 0;
    TaskId task = 0;
    std::string handler;
    std::string message;
    SourceLocation location;
    std::string stack;
    Microseconds duration{0};
    Microseconds limit{0};
    Microseconds window{0};
    std::size_t bytes = 0;
    std::size_t byteLimit = 0;
};

[[nodiscard]] std::string_view kindName(ReportKind kind) noexcept;
[[nodiscard]] std::string describe(const Report& report);

}
