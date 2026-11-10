#include "DemoFeed.h"

#include <gs/render/FrameSlot.h>
#include <gs/render/GraphicsDevice.h>
#include <gs/render/VideoItem.h>

#include <doctest/doctest.h>

#include <QSGNode>
#include <QSize>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

using gs::app::DemoFeed;
using gs::media::VideoFrame;
using gs::render::FrameSlot;
using gs::render::GraphicsDevice;
using gs::render::VideoItem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

class PaintedVideo final : public VideoItem
{
public:
    PaintedVideo() = default;
    ~PaintedVideo() override { delete m_node; }
    PaintedVideo(const PaintedVideo&) = delete;
    PaintedVideo& operator=(const PaintedVideo&) = delete;

    QSize paint()
    {
        m_node = updatePaintNode(m_node, nullptr);
        return frameSize();
    }

private:
    QSGNode* m_node = nullptr;
};

}

TEST_CASE("a frozen picture keeps its frame while new frames are taken, and shows the next one once unfrozen")
{
    const auto graphics = GraphicsDevice::create();
    REQUIRE(graphics);
    DemoFeed feed(*graphics, std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / "emulator_h264.media");
    REQUIRE(feed.start());
    VideoFrame portrait;
    VideoFrame landscape;
    const auto deadline = Clock::now() + 10s;
    while ((!portrait || !landscape) && Clock::now() < deadline) {
        VideoFrame frame = feed.frames()->take();
        if (!frame) {
            std::this_thread::sleep_for(5ms);
            continue;
        }
        VideoFrame& wanted = frame.height() > frame.width() ? portrait : landscape;
        if (!wanted) {
            wanted = std::move(frame);
        }
    }
    feed.stop();
    REQUIRE(portrait);
    REQUIRE(landscape);
    const QSize upright(portrait.width(), portrait.height());
    const QSize wide(landscape.width(), landscape.height());

    const auto slot = std::make_shared<FrameSlot>();
    PaintedVideo video;
    video.setSource(slot);
    slot->put(portrait.share());
    CHECK(video.paint() == upright);

    video.setFrozen(true);
    CHECK(video.frozen());
    slot->put(landscape.share());
    const std::uint64_t taken = slot->taken();
    CHECK(video.paint() == upright);
    CHECK(slot->taken() == taken + 1);
    slot->put(landscape.share());
    CHECK(video.paint() == upright);

    video.setFrozen(false);
    CHECK_FALSE(video.frozen());
    CHECK(video.paint() == upright);
    slot->put(landscape.share());
    CHECK(video.paint() == wide);
}

TEST_CASE("frames that arrive together are shown one per refresh when the screen is faster than the stream, newest wins when it is not")
{
    const auto graphics = GraphicsDevice::create();
    REQUIRE(graphics);
    DemoFeed feed(*graphics, std::filesystem::path(GS_VIDEO_TEST_DATA_DIR) / "emulator_h265.media");
    REQUIRE(feed.start());
    std::vector<VideoFrame> frames;
    const auto deadline = Clock::now() + 10s;
    while (frames.size() < 4 && Clock::now() < deadline) {
        VideoFrame frame = feed.frames()->take();
        if (!frame) {
            std::this_thread::sleep_for(2ms);
            continue;
        }
        if (frames.empty() || frame.ptsUs() > frames.back().ptsUs()) {
            frames.push_back(std::move(frame));
        }
    }
    feed.stop();
    REQUIRE(frames.size() == 4);
    const auto gapUs = frames[3].ptsUs() - frames[2].ptsUs();
    REQUIRE(gapUs > 6'000);

    FrameSlot fast;
    fast.setDisplayIntervalUs(6'000);
    fast.put(frames[0].share());
    fast.put(frames[1].share());
    fast.put(frames[2].share());
    CHECK(fast.paced());
    CHECK(fast.replaced() == 1);
    VideoFrame first = fast.take();
    REQUIRE(first);
    CHECK(first.ptsUs() == frames[1].ptsUs());
    CHECK(fast.pending());
    VideoFrame second = fast.take();
    REQUIRE(second);
    CHECK(second.ptsUs() == frames[2].ptsUs());
    CHECK_FALSE(fast.pending());

    FrameSlot slow;
    slow.setDisplayIntervalUs(gapUs * 4);
    slow.put(frames[0].share());
    slow.put(frames[1].share());
    slow.put(frames[2].share());
    CHECK_FALSE(slow.paced());
    CHECK(slow.replaced() == 2);
    VideoFrame newest = slow.take();
    REQUIRE(newest);
    CHECK(newest.ptsUs() == frames[2].ptsUs());
    CHECK_FALSE(slow.pending());

    FrameSlot immediate;
    immediate.setDisplayIntervalUs(0);
    immediate.put(frames[0].share());
    immediate.put(frames[1].share());
    CHECK(immediate.paced());
    CHECK(immediate.replaced() == 0);
    CHECK(immediate.take().ptsUs() == frames[0].ptsUs());
    CHECK(immediate.take().ptsUs() == frames[1].ptsUs());
}
