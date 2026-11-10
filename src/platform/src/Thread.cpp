#include "gs/platform/Thread.h"

#include <windows.h>

#include <string>

namespace gs::platform {

void setCurrentThreadName(std::string_view name) noexcept
{
    try {
        const std::wstring wide(name.begin(), name.end());
        SetThreadDescription(GetCurrentThread(), wide.c_str());
    } catch (...) {
    }
}

void setCurrentThreadRole(ThreadRole role) noexcept
{
    SetThreadPriority(GetCurrentThread(),
                      role == ThreadRole::Input ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_ABOVE_NORMAL);
}

bool keepTimerResolution() noexcept
{
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;
    return SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)) != FALSE;
}

void returnFreeHeapMemory() noexcept
{
    HEAP_OPTIMIZE_RESOURCES_INFORMATION optimize{};
    optimize.Version = HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION;
    static_cast<void>(HeapSetInformation(nullptr, HeapOptimizeResources, &optimize, sizeof(optimize)));
}

std::int64_t millisecondsSinceProcessStart() noexcept
{
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return -1;
    }
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    const auto ticks = [](const FILETIME& time) {
        return static_cast<std::int64_t>((static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
    };
    return (ticks(now) - ticks(created)) / 10'000;
}

}
