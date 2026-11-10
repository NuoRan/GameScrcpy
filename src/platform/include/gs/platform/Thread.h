#pragma once

#include <cstdint>
#include <string_view>

namespace gs::platform {

enum class ThreadRole : std::uint8_t { Input, Media };

void setCurrentThreadName(std::string_view name) noexcept;

void setCurrentThreadRole(ThreadRole role) noexcept;

[[nodiscard]] bool keepTimerResolution() noexcept;

void returnFreeHeapMemory() noexcept;

[[nodiscard]] std::int64_t millisecondsSinceProcessStart() noexcept;

}
