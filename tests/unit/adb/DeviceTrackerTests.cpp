#include "FakeAdbServer.h"

#include "gs/adb/DeviceTracker.h"

#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

using namespace gs::adb;
using gs::adb::test::closedPort;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;

namespace {

class Recorder
{
public:
    DeviceTracker::Callback callback()
    {
        return [this](const TrackerUpdate& update) {
            {
                const std::lock_guard lock(m_mutex);
                m_updates.push_back(update);
            }
            m_changed.notify_all();
        };
    }

    bool waitFor(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, timeout, [&] { return m_updates.size() >= count; });
    }

    std::vector<TrackerUpdate> updates()
    {
        const std::lock_guard lock(m_mutex);
        return m_updates;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<TrackerUpdate> m_updates;
};

std::string serials(const TrackerUpdate& update)
{
    std::string text;
    for (const DeviceEntry& device : update.devices) {
        text += device.serial + ";";
    }
    return text;
}

}

TEST_CASE("device tracker: pushed lists, an outage and the reconnection")
{
    int connection = 0;
    FakeAdbServer server([&](FakeConnection& c) {
        CHECK(c.readRequest() == "host:track-devices-l");
        c.write("OKAY");
        if (connection++ == 0) {
            c.write(FakeConnection::prefixed("A\tdevice transport_id:1\n"));
            c.write(FakeConnection::prefixed("A\tdevice transport_id:1\nB\toffline transport_id:2\n"));
            return;
        }
        c.write(FakeConnection::prefixed("B\tdevice transport_id:2\n"));
        CHECK(c.waitForClientClose());
    });
    Recorder recorder;
    std::optional<DeviceTracker> tracker;
    tracker.emplace(server.port(), recorder.callback());
    REQUIRE(recorder.waitFor(4));
    tracker.reset();

    const auto updates = recorder.updates();
    REQUIRE(updates.size() == 4);
    CHECK(updates[0].serverAvailable);
    CHECK(serials(updates[0]) == "A;");
    CHECK(serials(updates[1]) == "A;B;");
    CHECK(updates[1].devices[1].state == DeviceState::Offline);
    CHECK_FALSE(updates[2].serverAvailable);
    CHECK(updates[2].devices.empty());
    CHECK(updates[3].serverAvailable);
    CHECK(serials(updates[3]) == "B;");
}

TEST_CASE("device tracker: destruction is prompt while blocked on a silent server")
{
    FakeAdbServer server([](FakeConnection& c) {
        (void)c.readRequest();
        c.write("OKAY" + FakeConnection::prefixed(""));
        CHECK(c.waitForClientClose());
    });
    Recorder recorder;
    std::optional<DeviceTracker> tracker;
    tracker.emplace(server.port(), recorder.callback());
    REQUIRE(recorder.waitFor(1));
    CHECK(recorder.updates()[0].serverAvailable);
    CHECK(recorder.updates()[0].devices.empty());

    const auto start = std::chrono::steady_clock::now();
    tracker.reset();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    MESSAGE("tracker stopped in " << std::chrono::duration<double, std::milli>(elapsed).count() << " ms");
    CHECK(elapsed < std::chrono::milliseconds(200));
}

TEST_CASE("device tracker: an absent server is reported once across retries")
{
    FakeAdbServer server([](FakeConnection&) {});
    Recorder recorder;
    std::optional<DeviceTracker> tracker;
    tracker.emplace(server.port(), recorder.callback());
    REQUIRE(server.waitForConnections(3));
    tracker.reset();

    const auto updates = recorder.updates();
    REQUIRE(updates.size() == 1);
    CHECK_FALSE(updates[0].serverAvailable);
}

TEST_CASE("device tracker: no server at all, stopped during the backoff")
{
    Recorder recorder;
    std::optional<DeviceTracker> tracker;
    tracker.emplace(closedPort(), recorder.callback());
    REQUIRE(recorder.waitFor(1));
    const auto start = std::chrono::steady_clock::now();
    tracker.reset();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200));
    CHECK_FALSE(recorder.updates()[0].serverAvailable);
}
