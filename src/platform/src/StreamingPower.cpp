#include "gs/platform/StreamingPower.h"

#include "gs/base/Log.h"
#include "gs/platform/Thread.h"

#include <windows.h>
#include <wlanapi.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace gs::platform {

namespace {

constexpr std::string_view kLog = "power";

struct WlanApi {
    decltype(&WlanOpenHandle) open = nullptr;
    decltype(&WlanCloseHandle) close = nullptr;
    decltype(&WlanEnumInterfaces) enumerate = nullptr;
    decltype(&WlanSetInterface) set = nullptr;
    decltype(&WlanFreeMemory) free = nullptr;
    DWORD loadError = ERROR_SUCCESS;

    [[nodiscard]] bool ready() const noexcept
    {
        return open != nullptr && close != nullptr && enumerate != nullptr && set != nullptr && free != nullptr;
    }
};

template <typename Function>
Function wlanEntry(HMODULE library, const char* name) noexcept
{
    return reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(library, name)));
}

const WlanApi& wlanApi() noexcept
{
    static const WlanApi api = [] {
        WlanApi loaded;
        const HMODULE library = LoadLibraryExW(L"wlanapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library == nullptr) {
            loaded.loadError = GetLastError();
            return loaded;
        }
        loaded.open = wlanEntry<decltype(&WlanOpenHandle)>(library, "WlanOpenHandle");
        loaded.close = wlanEntry<decltype(&WlanCloseHandle)>(library, "WlanCloseHandle");
        loaded.enumerate = wlanEntry<decltype(&WlanEnumInterfaces)>(library, "WlanEnumInterfaces");
        loaded.set = wlanEntry<decltype(&WlanSetInterface)>(library, "WlanSetInterface");
        loaded.free = wlanEntry<decltype(&WlanFreeMemory)>(library, "WlanFreeMemory");
        if (!loaded.ready()) {
            loaded.loadError = ERROR_PROC_NOT_FOUND;
        }
        return loaded;
    }();
    return api;
}

class WlanClient
{
public:
    WlanClient() noexcept = default;
    ~WlanClient()
    {
        std::uint32_t adapters = 0;
        static_cast<void>(apply(false, adapters));
    }
    WlanClient(const WlanClient&) = delete;
    WlanClient& operator=(const WlanClient&) = delete;

    [[nodiscard]] DWORD apply(bool on, std::uint32_t& adapters) noexcept
    {
        adapters = 0;
        if (!on) {
            if (m_handle == nullptr) {
                return ERROR_SUCCESS;
            }
            const DWORD code = setOnConnected(FALSE, adapters);
            close();
            return code;
        }
        const WlanApi& api = wlanApi();
        if (!api.ready()) {
            return api.loadError;
        }
        if (m_handle == nullptr) {
            DWORD version = 0;
            HANDLE handle = nullptr;
            if (const DWORD code = api.open(WLAN_API_VERSION_2_0, nullptr, &version, &handle);
                code != ERROR_SUCCESS) {
                return code;
            }
            m_handle = handle;
        }
        const DWORD code = setOnConnected(TRUE, adapters);
        if (adapters == 0) {
            close();
        }
        return code;
    }

private:
    DWORD setOnConnected(BOOL value, std::uint32_t& adapters) noexcept
    {
        const WlanApi& api = wlanApi();
        PWLAN_INTERFACE_INFO_LIST list = nullptr;
        if (const DWORD code = api.enumerate(m_handle, nullptr, &list); code != ERROR_SUCCESS) {
            return code;
        }
        DWORD firstError = ERROR_SUCCESS;
        for (DWORD index = 0; index < list->dwNumberOfItems; ++index) {
            const WLAN_INTERFACE_INFO& info = list->InterfaceInfo[index];
            if (info.isState != wlan_interface_state_connected) {
                continue;
            }
            const DWORD code = api.set(m_handle, &info.InterfaceGuid, wlan_intf_opcode_media_streaming_mode,
                                       sizeof(value), &value, nullptr);
            if (code == ERROR_SUCCESS) {
                ++adapters;
            } else if (firstError == ERROR_SUCCESS) {
                firstError = code;
            }
        }
        api.free(list);
        return firstError;
    }

    void close() noexcept
    {
        if (m_handle != nullptr) {
            wlanApi().close(m_handle, nullptr);
            m_handle = nullptr;
        }
    }

    HANDLE m_handle = nullptr;
};

}

struct SystemStreamingPower::Worker {
    void run(const std::stop_token& stop) noexcept;

    std::mutex mutex;
    std::condition_variable_any wake;
    bool wanted = false;
    std::uint64_t requested = 0;
    std::atomic<std::uint64_t> applied{0};
    std::jthread thread;
};

void SystemStreamingPower::Worker::run(const std::stop_token& stop) noexcept
{
    setCurrentThreadName("gs-wlan");
    WlanClient client;
    bool reported = false;
    std::uint64_t seen = 0;
    std::unique_lock lock(mutex);
    while (wake.wait(lock, stop, [&] { return requested != seen; })) {
        seen = requested;
        const bool on = wanted;
        lock.unlock();
        std::uint32_t adapters = 0;
        const DWORD code = client.apply(on, adapters);
        if (code == ERROR_SUCCESS) {
            log::debug(kLog, "media streaming mode {} on {} wireless adapter(s)", on ? "on" : "off", adapters);
        } else if (!reported) {
            reported = true;
            log::debug(kLog, "media streaming mode {} failed: {}", on ? "on" : "off", code);
        }
        applied.fetch_add(1, std::memory_order_release);
        lock.lock();
    }
}

void StreamingPower::begin(std::string_view session, StreamingLink link) noexcept
{
    GS_ASSERT_ON_THREAD(m_thread);
    if (const auto found = m_sessions.find(session); found != m_sessions.end()) {
        if (found->second != link) {
            found->second = link;
            refresh();
        }
        return;
    }
    if (m_sessions.size() >= kMaxSessions) {
        return;
    }
    try {
        m_sessions.emplace(std::string(session), link);
    } catch (...) {
        return;
    }
    refresh();
}

void StreamingPower::end(std::string_view session) noexcept
{
    GS_ASSERT_ON_THREAD(m_thread);
    const auto found = m_sessions.find(session);
    if (found == m_sessions.end()) {
        return;
    }
    m_sessions.erase(found);
    refresh();
}

std::uint32_t StreamingPower::streamingSessions() const noexcept
{
    return static_cast<std::uint32_t>(m_sessions.size());
}

std::uint32_t StreamingPower::wirelessSessions() const noexcept
{
    std::uint32_t count = 0;
    for (const auto& entry : m_sessions) {
        if (entry.second == StreamingLink::Wireless) {
            ++count;
        }
    }
    return count;
}

void StreamingPower::refresh() noexcept
{
    bool wireless = false;
    for (const auto& entry : m_sessions) {
        if (entry.second == StreamingLink::Wireless) {
            wireless = true;
            break;
        }
    }
    if (wireless != m_mediaStreaming) {
        m_mediaStreaming = wireless;
        const std::uint32_t code = m_target.setMediaStreaming(wireless);
        if (code == 0) {
            log::debug(kLog, "media streaming mode {} requested", wireless ? "on" : "off");
        } else if (!m_mediaReported) {
            m_mediaReported = true;
            log::debug(kLog, "media streaming mode {} failed: {}", wireless ? "on" : "off", code);
        }
    }
    const bool speed = !m_sessions.empty();
    if (speed != m_executionSpeed) {
        m_executionSpeed = speed;
        const std::uint32_t code = m_target.setExecutionSpeed(speed);
        if (code == 0) {
            log::debug(kLog, "process power {}", speed ? "kept out of EcoQoS" : "left to the system");
        } else if (!m_speedReported) {
            m_speedReported = true;
            log::debug(kLog, "execution speed {} failed: {}", speed ? "on" : "off", code);
        }
    }
}

SystemStreamingPower::SystemStreamingPower() noexcept = default;

SystemStreamingPower::~SystemStreamingPower() = default;

std::uint32_t SystemStreamingPower::setMediaStreaming(bool on) noexcept
{
    if (m_worker == nullptr) {
        if (!on) {
            return 0;
        }
        try {
            auto worker = std::make_unique<Worker>();
            worker->thread =
                std::jthread([raw = worker.get()](const std::stop_token& stop) { raw->run(stop); });
            m_worker = std::move(worker);
        } catch (...) {
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    {
        const std::lock_guard lock(m_worker->mutex);
        m_worker->wanted = on;
        ++m_worker->requested;
    }
    m_worker->wake.notify_one();
    return 0;
}

std::uint32_t SystemStreamingPower::setExecutionSpeed(bool on) noexcept
{
    const auto apply = [](ULONG control) -> DWORD {
        PROCESS_POWER_THROTTLING_STATE state{};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = control;
        state.StateMask = 0;
        if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)) == FALSE) {
            return GetLastError();
        }
        return ERROR_SUCCESS;
    };
    const ULONG speed = on ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    DWORD code = apply(PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION | speed);
    if (code == ERROR_INVALID_PARAMETER) {
        code = apply(speed);
    }
    return code;
}

std::uint64_t SystemStreamingPower::mediaStreamingApplied() const noexcept
{
    return m_worker == nullptr ? 0 : m_worker->applied.load(std::memory_order_acquire);
}

}
