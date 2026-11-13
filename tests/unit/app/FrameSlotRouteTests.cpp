#include <gs/media/VideoDecoder.h>
#include <gs/render/FrameSlot.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

using gs::media::VideoFrame;
using gs::render::FrameSlot;

namespace {

VideoFrame makeFrame(int width, int height, std::int64_t ptsUs)
{
    const auto chromaPitch = (width + 1) / 2 * 2;
    std::vector<std::uint8_t> luma(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 16);
    std::vector<std::uint8_t> chroma(static_cast<std::size_t>(chromaPitch) * static_cast<std::size_t>((height + 1) / 2),
                                     128);
    return VideoFrame::copyNv12(luma.data(), width, chroma.data(), chromaPitch, width, height, ptsUs, 1, 0);
}

}

TEST_CASE("ADR-0114: a frame wakes the interface route only when it finds the slot empty")
{
    FrameSlot slot;
    int gui = 0;
    slot.setWaker([&gui] { ++gui; });
    slot.put(makeFrame(4, 4, 1'000));
    CHECK(gui == 1);
    slot.put(makeFrame(4, 4, 2'000));
    CHECK(gui == 1);
    (void)slot.take();
    (void)slot.take();
    slot.wakeGui();
    CHECK(gui == 2);
}

TEST_CASE("ADR-0114: an installed render route takes the wake instead of the interface route")
{
    FrameSlot slot;
    int gui = 0;
    int render = 0;
    slot.setWaker([&gui] { ++gui; });
    slot.setRenderWaker(&render, [&render] { ++render; });
    slot.put(makeFrame(4, 4, 1'000));
    CHECK(render == 1);
    CHECK(gui == 0);
    slot.clearRenderWaker(&gui);
    (void)slot.take();
    slot.put(makeFrame(4, 4, 2'000));
    CHECK(render == 2);
    CHECK(gui == 0);
    slot.clearRenderWaker(&render);
    (void)slot.take();
    slot.put(makeFrame(4, 4, 3'000));
    CHECK(render == 2);
    CHECK(gui == 1);
}

TEST_CASE("ADR-0114: the screen listener sees every recorded frame")
{
    FrameSlot slot;
    std::vector<std::int64_t> screens;
    slot.setScreenListener(
        [&screens](std::int64_t, std::int64_t, std::int64_t, std::int64_t screenUs) { screens.push_back(screenUs); });
    slot.recordScreen(1, 2, 3, 4);
    slot.recordScreen(5, 6, 7, 8);
    REQUIRE(screens.size() == 2);
    CHECK(screens[0] == 4);
    CHECK(screens[1] == 8);
    slot.setScreenListener({});
    slot.recordScreen(9, 10, 11, 12);
    CHECK(screens.size() == 2);
}

TEST_CASE("ADR-0114: screen statistics give the mean and the 95th percentile, then reset")
{
    FrameSlot slot;
    for (int i = 1; i <= 100; ++i) {
        const std::int64_t screenUs = static_cast<std::int64_t>(i) * 1'000;
        slot.recordScreen(static_cast<std::int64_t>(i), 0, 1'000, screenUs);
    }
    const std::optional<gs::render::ScreenStats> stats = slot.takeScreenStats();
    REQUIRE(stats);
    CHECK(stats->arrivalToScreenMs == doctest::Approx(50.5));
    CHECK(stats->arrivalToScreenP95Ms == doctest::Approx(95.0));
    CHECK(stats->renderToScreenMs == doctest::Approx(49.5));
    CHECK(stats->renderToScreenP95Ms == doctest::Approx(94.0));
    CHECK_FALSE(slot.takeScreenStats());
}

TEST_CASE("ADR-0114: screen samples per interval stay bounded")
{
    FrameSlot slot;
    for (int i = 0; i < 2'000; ++i) {
        slot.recordScreen(0, 0, 0, static_cast<std::int64_t>(i) * 1'000);
    }
    const std::optional<gs::render::ScreenStats> stats = slot.takeScreenStats();
    REQUIRE(stats);
    CHECK(stats->arrivalToScreenMs == doctest::Approx((976.0 + 1'999.0) / 2.0));
}

TEST_CASE("ADR-0114: the presentation mode is kept until it changes")
{
    FrameSlot slot;
    CHECK(slot.presentationMode() == gs::render::PresentationMode::Unknown);
    slot.setPresentationMode(gs::render::PresentationMode::IndependentFlip);
    CHECK(slot.presentationMode() == gs::render::PresentationMode::IndependentFlip);
}
