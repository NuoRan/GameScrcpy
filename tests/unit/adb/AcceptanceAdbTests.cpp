#include "FakeAdbServer.h"

#include "gs/adb/DeviceIdentity.h"
#include "gs/adb/DeviceTracker.h"

#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace gs::adb;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;
using Clock = std::chrono::steady_clock;

namespace {

class PushedLists
{
public:
    void push(std::string list)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_lists.push_back(std::move(list));
        }
        m_changed.notify_all();
    }

    void finish()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_finished = true;
        }
        m_changed.notify_all();
    }

    void serve(FakeConnection& connection)
    {
        if (connection.readRequest() != "host:track-devices-l") {
            return;
        }
        connection.write("OKAY");
        std::size_t written = 0;
        for (;;) {
            std::string list;
            {
                std::unique_lock lock(m_mutex);
                if (!m_changed.wait_for(lock, std::chrono::seconds(10),
                                        [&] { return m_finished || m_lists.size() > written; }) ||
                    m_lists.size() <= written) {
                    return;
                }
                list = m_lists[written];
                m_sent.push_back(Clock::now());
            }
            connection.write(FakeConnection::prefixed(list));
            ++written;
        }
    }

    [[nodiscard]] std::vector<Clock::time_point> sent() const
    {
        const std::lock_guard lock(m_mutex);
        return m_sent;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<std::string> m_lists;
    std::vector<Clock::time_point> m_sent;
    bool m_finished = false;
};

class FinishedAtExit
{
public:
    explicit FinishedAtExit(PushedLists& lists) : m_lists(lists) {}
    ~FinishedAtExit() { m_lists.finish(); }
    FinishedAtExit(const FinishedAtExit&) = delete;
    FinishedAtExit& operator=(const FinishedAtExit&) = delete;

private:
    PushedLists& m_lists;
};

class TrackerLog
{
public:
    DeviceTracker::Callback callback()
    {
        return [this](const TrackerUpdate& update) {
            {
                const std::lock_guard lock(m_mutex);
                m_updates.push_back(update);
                m_times.push_back(Clock::now());
            }
            m_changed.notify_all();
        };
    }

    bool waitFor(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_updates.size() >= count; });
    }

    [[nodiscard]] std::vector<TrackerUpdate> updates() const
    {
        const std::lock_guard lock(m_mutex);
        return m_updates;
    }

    [[nodiscard]] std::vector<Clock::time_point> times() const
    {
        const std::lock_guard lock(m_mutex);
        return m_times;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<TrackerUpdate> m_updates;
    std::vector<Clock::time_point> m_times;
};

double millisecondsBetween(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

std::string shellPacket(char id, std::string_view data)
{
    std::string packet(1, id);
    const auto size = static_cast<std::uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) {
        packet.push_back(static_cast<char>((size >> (8 * i)) & 0xFFU));
    }
    return packet + std::string(data);
}

}

TEST_CASE("ADB-002: a phone plugged in or unplugged reaches the device list within 200 ms of the adb server's push")
{
    PushedLists lists;
    FakeAdbServer server([&lists](FakeConnection& connection) { lists.serve(connection); });
    const FinishedAtExit finishing(lists);
    TrackerLog tracked;
    std::optional<DeviceTracker> tracker;
    tracker.emplace(server.port(), tracked.callback());

    lists.push("");
    REQUIRE(tracked.waitFor(1));
    lists.push("R58M123ABC\tdevice usb:1-4 product:e3q model:SM_S921B device:e3q transport_id:3\n");
    REQUIRE(tracked.waitFor(2));
    lists.push("");
    REQUIRE(tracked.waitFor(3));
    tracker.reset();

    const std::vector<TrackerUpdate> updates = tracked.updates();
    const std::vector<Clock::time_point> received = tracked.times();
    const std::vector<Clock::time_point> sent = lists.sent();
    REQUIRE(updates.size() >= 3);
    REQUIRE(received.size() >= 3);
    REQUIRE(sent.size() == 3);
    CHECK(updates[0].serverAvailable);
    CHECK(updates[0].devices.empty());
    CHECK(updates[1].serverAvailable);
    REQUIRE(updates[1].devices.size() == 1);
    CHECK(updates[1].devices[0].serial == "R58M123ABC");
    CHECK(updates[1].devices[0].state == DeviceState::Device);
    CHECK(updates[1].devices[0].model == "SM_S921B");
    CHECK(updates[2].serverAvailable);
    CHECK(updates[2].devices.empty());

    const double pluggedMs = millisecondsBetween(sent[1], received[1]);
    const double unpluggedMs = millisecondsBetween(sent[2], received[2]);
    MESSAGE("pushed list to device list: plugged in " << pluggedMs << " ms, unplugged " << unpluggedMs << " ms");
    CHECK(pluggedMs < 200.0);
    CHECK(unpluggedMs < 200.0);
}

TEST_CASE("ADB-006: one phone seen over USB, wireless debugging and adb tcpip reads as one device")
{
    constexpr std::string_view kUsb = "R58M123ABC";
    constexpr std::string_view kWireless = "adb-R58M123ABC-AbCdEf._adb-tls-connect._tcp";
    constexpr std::string_view kTcpip = "192.168.1.20:5555";
    const std::string answer = "serialno=R58M123ABC\nbootserialno=R58M123ABC\nandroidid=ab3f31256ae3d44c\n"
                               "release=14\nmanufacturer=samsung\nmodel=SM-S921B\nmarketname=Galaxy S24\nbooted=1\n";
    std::mutex mutex;
    std::vector<std::string> asked;
    FakeAdbServer server([&](FakeConnection& connection) {
        constexpr std::string_view kTransport = "host:transport:";
        const std::string request = connection.readRequest();
        if (!request.starts_with(kTransport)) {
            connection.write("FAIL" + FakeConnection::prefixed("unexpected request"));
            return;
        }
        {
            const std::lock_guard lock(mutex);
            asked.push_back(request.substr(kTransport.size()));
        }
        connection.write("OKAY");
        const std::string command = connection.readRequest();
        CHECK(command.starts_with("shell,v2,raw:"));
        connection.write("OKAY" + shellPacket(1, answer) + shellPacket(3, std::string(1, '\0')));
    });
    const AdbClient client(server.port());

    const auto overUsb = readDeviceInfo(client, kUsb);
    const auto overWireless = readDeviceInfo(client, kWireless);
    const auto overTcpip = readDeviceInfo(client, kTcpip);
    REQUIRE(overUsb);
    REQUIRE(overWireless);
    REQUIRE(overTcpip);
    CHECK(overUsb->id == "R58M123ABC");
    CHECK(overUsb->marketName == "Galaxy S24");
    CHECK(overWireless->id == overUsb->id);
    CHECK(overTcpip->id == overUsb->id);
    CHECK(*overWireless == *overUsb);
    CHECK(*overTcpip == *overUsb);

    const std::lock_guard lock(mutex);
    CHECK(asked == std::vector<std::string>{std::string(kUsb), std::string(kWireless), std::string(kTcpip)});
}
