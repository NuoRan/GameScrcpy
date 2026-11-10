#include "gs/adb/DeviceTracker.h"

#include "HostProtocol.h"

#include <algorithm>
#include <utility>

namespace gs::adb {

DeviceTracker::DeviceTracker(std::uint16_t port, Callback callback)
    : m_port(port), m_callback(std::move(callback)), m_thread([this](std::stop_token stop) { run(stop); })
{
}

DeviceTracker::~DeviceTracker()
{
    m_thread.request_stop();
    m_thread.join();
}

void DeviceTracker::run(const std::stop_token& stop)
{
    std::chrono::milliseconds backoff = kMinBackoff;
    while (!stop.stop_requested()) {
        if (follow(stop)) {
            backoff = kMinBackoff;
        }
        if (stop.stop_requested()) {
            break;
        }
        report(TrackerUpdate{});
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, stop, backoff, [] { return false; });
        backoff = std::min(backoff * 2, kMaxBackoff);
    }
}

bool DeviceTracker::follow(const std::stop_token& stop)
{
    AdbResult<detail::TcpStream> stream = detail::connectServer(m_port, std::chrono::milliseconds::zero());
    if (!stream) {
        return false;
    }
    const std::stop_callback interrupt(stop, [&stream]() noexcept { stream->interrupt(); });
    if (!detail::request(*stream, "host:track-devices-l")) {
        return false;
    }
    for (;;) {
        AdbResult<std::string> list = detail::readLengthPrefixed(*stream);
        if (!list) {
            return true;
        }
        report(TrackerUpdate{true, parseDeviceList(*list)});
    }
}

void DeviceTracker::report(TrackerUpdate update)
{
    if (!update.serverAvailable && m_lastAvailable == false) {
        return;
    }
    m_lastAvailable = update.serverAvailable;
    m_callback(update);
}

}
