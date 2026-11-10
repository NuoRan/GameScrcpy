#pragma once

#include <gs/adb/AdbClient.h>
#include <gs/adb/DeviceIdentity.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gs::app {

class DeviceIdentifier
{
public:
    using Done = std::function<void(const std::string& serial, const adb::AdbResult<adb::DeviceInfo>& info)>;

    DeviceIdentifier(adb::AdbClient client, Done done);
    ~DeviceIdentifier();
    DeviceIdentifier(const DeviceIdentifier&) = delete;
    DeviceIdentifier& operator=(const DeviceIdentifier&) = delete;

    void identify(std::string serial);

private:
    void run(const std::stop_token& stop);

    const adb::AdbClient m_client;
    const Done m_done;

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<std::string> m_queue;

    std::jthread m_worker;
};

}
