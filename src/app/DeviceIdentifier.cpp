#include "DeviceIdentifier.h"

#include <utility>

namespace gs::app {

DeviceIdentifier::DeviceIdentifier(adb::AdbClient client, Done done)
    : m_client(client),
      m_done(std::move(done)),
      m_worker([this](const std::stop_token& stop) { run(stop); })
{
}

DeviceIdentifier::~DeviceIdentifier()
{
    m_worker.request_stop();
    m_wake.notify_all();
}

void DeviceIdentifier::identify(std::string serial)
{
    {
        const std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(serial));
    }
    m_wake.notify_one();
}

void DeviceIdentifier::run(const std::stop_token& stop)
{
    for (;;) {
        std::string serial;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return !m_queue.empty(); })) {
                return;
            }
            serial = std::move(m_queue.front());
            m_queue.pop_front();
        }
        m_done(serial, adb::readDeviceInfo(m_client, serial));
    }
}

}
