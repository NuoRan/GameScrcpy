#include "DemoFeed.h"

#include <gs/render/FrameSlot.h>
#include <gs/render/GraphicsDevice.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>

using gs::app::DemoFeed;
using gs::media::VideoFrame;
using gs::render::FrameSlot;
using gs::render::GraphicsDevice;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

std::filesystem::path recording(const char* name)
{
    return std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / name;
}

class Arrivals
{
public:
    explicit Arrivals(FrameSlot& slot) : m_slot(slot)
    {
        m_slot.setWaker([this] {
            {
                const std::scoped_lock lock(m_mutex);
                ++m_wakes;
            }
            m_arrived.notify_all();
        });
    }

    ~Arrivals() { m_slot.setWaker({}); }

    Arrivals(const Arrivals&) = delete;
    Arrivals& operator=(const Arrivals&) = delete;

    VideoFrame next(Clock::time_point deadline)
    {
        std::unique_lock lock(m_mutex);
        for (;;) {
            const std::uint64_t seen = m_wakes;
            lock.unlock();
            VideoFrame frame = m_slot.take();
            lock.lock();
            if (frame || !m_arrived.wait_until(lock, deadline, [&] { return m_wakes != seen; })) {
                return frame;
            }
        }
    }

    bool arrives(Clock::time_point deadline) { return static_cast<bool>(next(deadline)); }

private:
    FrameSlot& m_slot;
    std::mutex m_mutex;
    std::condition_variable m_arrived;
    std::uint64_t m_wakes = 0;
};

}

TEST_CASE("the demo feed plays the recording at its pace into the frame slot, in a loop, until stopped")
{
    const auto graphics = GraphicsDevice::create();
    REQUIRE(graphics);
    DemoFeed feed(*graphics, recording("emulator_h264.media"));
    REQUIRE(feed.start());
    CHECK(feed.problem().isEmpty());
    const std::shared_ptr<FrameSlot> slot = feed.frames();
    REQUIRE(slot);
    Arrivals arrivals(*slot);

    const VideoFrame first = arrivals.next(Clock::now() + 3s);
    REQUIRE(first.width() > 0);
    CHECK(first.height() > 0);

    std::uint64_t shown = 1;
    const auto deadline = Clock::now() + 4s;
    while (feed.framesPerSecond() <= 0 && Clock::now() < deadline) {
        if (arrivals.arrives(std::min(deadline, Clock::now() + 100ms))) {
            ++shown;
        }
    }
    CHECK(feed.framesPerSecond() > 0);
    CHECK(feed.framesPerSecond() < 100);
    CHECK(shown > 1);

    feed.stop();
    CHECK(feed.framesPerSecond() == 0);
    static_cast<void>(slot->take());
    const std::uint64_t replaced = slot->replaced();
    CHECK_FALSE(arrivals.arrives(Clock::now() + 300ms));
    CHECK(slot->replaced() == replaced);
    feed.stop();

    REQUIRE(feed.start());
    CHECK(feed.frames().get() == slot.get());
    CHECK(arrivals.arrives(Clock::now() + 3s));
}

TEST_CASE("the demo feed makes up a phone status that changes every two seconds and stays in range")
{
    using gs::protocol::DeviceStatus;
    const DeviceStatus first = DemoFeed::fakeStatus(0ms);
    CHECK(first.batteryPercent == 82);
    CHECK_FALSE(first.charging);
    CHECK(first.temperatureDeciC == 352);
    CHECK(first.cpuPermille == 230);
    CHECK(DemoFeed::fakeStatus(1999ms) == first);
    CHECK(DemoFeed::fakeStatus(-5s) == first);
    CHECK(DemoFeed::fakeStatus(2s).cpuPermille != first.cpuPermille);
    CHECK(DemoFeed::fakeStatus(2s).temperatureDeciC != first.temperatureDeciC);

    bool charged = false;
    bool drained = false;
    for (int step = 0; step < 100; ++step) {
        CAPTURE(step);
        const DeviceStatus status = DemoFeed::fakeStatus(std::chrono::milliseconds(step * 2000));
        CHECK(DeviceStatus::isValid(status));
        CHECK(status.batteryPercent >= 78);
        CHECK(status.batteryPercent <= 82);
        CHECK(status.temperatureDeciC != DeviceStatus::kUnknownTemperature);
        CHECK(status.cpuPermille <= DeviceStatus::kMaxCpu);
        charged = charged || status.charging;
        drained = drained || !status.charging;
    }
    CHECK(charged);
    CHECK(drained);
    CHECK(DemoFeed::fakeStatus(40s).charging);
    CHECK(DemoFeed::fakeStatus(40s).batteryPercent == 78);
}

TEST_CASE("the demo feed refuses a recording it cannot read, with a reason")
{
    const auto graphics = GraphicsDevice::create();
    REQUIRE(graphics);
    DemoFeed feed(*graphics, recording("missing.media"));
    CHECK_FALSE(feed.start());
    CHECK_FALSE(feed.problem().isEmpty());
    CHECK(feed.framesPerSecond() == 0);
    feed.stop();
    feed.stop();
}

TEST_CASE("the demo feed refuses a file that is not a video recording")
{
    const auto graphics = GraphicsDevice::create();
    REQUIRE(graphics);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "gs_demo_feed_not_a_recording.media";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "this is not a recording of a video channel";
    }
    DemoFeed feed(*graphics, path);
    CHECK_FALSE(feed.start());
    CHECK(feed.problem().contains(QStringLiteral("not a video recording")));
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}
