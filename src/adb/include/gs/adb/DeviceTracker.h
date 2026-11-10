#pragma once

#include "gs/adb/DeviceList.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace gs::adb {

struct TrackerUpdate {
    bool serverAvailable = false;
    std::vector<DeviceEntry> devices;
};

class DeviceTracker
{
public:
    using Callback = std::function<void(const TrackerUpdate&)>;

    static constexpr std::chrono::milliseconds kMinBackoff{250};
    static constexpr std::chrono::milliseconds kMaxBackoff{2000};

    DeviceTracker(std::uint16_t port, Callback callback);
    ~DeviceTracker();

    DeviceTracker(const DeviceTracker&) = delete;
    DeviceTracker& operator=(const DeviceTracker&) = delete;

private:
    void run(const std::stop_token& stop);
    bool follow(const std::stop_token& stop);
    void report(TrackerUpdate update);

    std::uint16_t m_port;
    Callback m_callback;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::optional<bool> m_lastAvailable;
    std::jthread m_thread;
};

}
