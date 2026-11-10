#include "LatencyMeter.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

using gs::app::LatencyMeter;
using gs::app::LatencyMeterOptions;
using gs::media::LumaReading;
using gs::protocol::TouchAction;

namespace {

constexpr int kWidth = 1080;
constexpr int kHeight = 2400;
constexpr double kPixelsPerDp = 2.75;
constexpr std::int64_t kFrameUs = 16'667;
constexpr std::int64_t kRenderUs = 5'000;

const LumaReading kBackground{60, 60};
const LumaReading kDot{200, 60};

gs::protocol::TouchFrame touch(TouchAction action, std::uint8_t pointer, double u, double v)
{
    gs::protocol::TouchFrame frame;
    frame.videoSession = 1;
    gs::protocol::TouchPoint point;
    point.action = action;
    point.pointerId = pointer;
    point.x = static_cast<std::uint16_t>(std::lround(u * 65535));
    point.y = static_cast<std::uint16_t>(std::lround(v * 65535));
    point.pressure = action == TouchAction::Up ? 0 : 0xFFFF;
    frame.points.push_back(point);
    return frame;
}

struct World {
    World() = default;
    explicit World(LatencyMeterOptions options) : meter(options) {}

    LatencyMeter meter;
    std::int64_t nowUs = 10'000'000;
    std::int64_t ptsUs = 50'000'000;
    LumaReading previous = kBackground;
    int plannedFresh = 0;
    int plannedWatched = 0;

    void act(TouchAction action, double u, double v, std::uint8_t pointer = 0)
    {
        meter.touched(touch(action, pointer, u, v), nowUs);
    }

    void frame(LumaReading look, std::int64_t gapUs = kFrameUs)
    {
        nowUs += gapUs;
        ptsUs += gapUs;
        const LatencyMeter::Plan plan = meter.plan(kWidth, kHeight, kPixelsPerDp, nowUs);
        plannedFresh = static_cast<int>(plan.fresh.size());
        plannedWatched = static_cast<int>(plan.watched.size());
        const std::vector<LumaReading> fresh(plan.fresh.size(), previous);
        const std::vector<LumaReading> watched(plan.watched.size(), look);
        meter.observe(plan, fresh, watched, ptsUs, nowUs);
        meter.shown(ptsUs, nowUs + kRenderUs);
        previous = look;
    }

    void frames(int count, LumaReading look)
    {
        for (int i = 0; i < count; ++i) {
            frame(look);
        }
    }

    void measureOnce(int framesUntilDot, double u, double v)
    {
        act(TouchAction::Down, u, v);
        frames(framesUntilDot, kBackground);
        frames(2, kDot);
        act(TouchAction::Up, u, v);
        frames(2, kBackground);
    }
};

double expectedMs(int framesUntilDot)
{
    return static_cast<double>((framesUntilDot + 1) * kFrameUs + kRenderUs) / 1000.0;
}

}

TEST_CASE("latency meter: a touch dot seen in two frames in a row times the press to its render")
{
    World world;
    world.frame(kBackground);
    world.act(TouchAction::Down, 0.5, 0.5);
    world.frame(kBackground);
    CHECK(world.plannedFresh == 1);
    world.frame(kBackground);
    CHECK(world.plannedWatched == 1);
    world.frames(2, kDot);
    const auto progress = world.meter.progress();
    CHECK(progress.running);
    CHECK(progress.measured == 1);
    CHECK(progress.missed == 0);
    CHECK(progress.target == 10);
}

TEST_CASE("latency meter: ten presses give the median, the lowest and the highest, then it stops")
{
    World world;
    world.frame(kBackground);
    const int framesUntilDot[] = {1, 2, 3, 1, 2, 3, 4, 1, 2, 5};
    for (int i = 0; i < 10; ++i) {
        world.measureOnce(framesUntilDot[i], 0.1 + 0.2 * (i % 5), i < 5 ? 0.2 : 0.7);
    }
    const auto progress = world.meter.progress();
    CHECK_FALSE(progress.running);
    CHECK(progress.measured == 10);
    REQUIRE(progress.result);
    CHECK(progress.result->samples == 10);
    CHECK(progress.result->medianMs == doctest::Approx(expectedMs(2)));
    CHECK(progress.result->lowMs == doctest::Approx(expectedMs(1)));
    CHECK(progress.result->highMs == doctest::Approx(expectedMs(5)));

    world.act(TouchAction::Down, 0.5, 0.5);
    world.frame(kBackground);
    CHECK(world.plannedFresh == 0);
}

TEST_CASE("latency meter: on a still screen the next frame may come late, and the first change counts")
{
    World world;
    world.frame(kBackground);
    world.act(TouchAction::Down, 0.5, 0.5);
    world.frame(kBackground);
    world.frame(kDot);
    world.act(TouchAction::Up, 0.5, 0.5);
    world.frame(kBackground, 100'000);
    CHECK(world.meter.progress().measured == 1);
}

TEST_CASE("latency meter: a change gone by the next frame, or one across the whole area, is not a dot")
{
    World world;
    world.frame(kBackground);
    world.act(TouchAction::Down, 0.5, 0.5);
    world.frame(kBackground);
    world.frame(kDot);
    world.frame(kBackground);
    world.frames(3, LumaReading{200, 190});
    world.frames(60, kBackground);
    const auto progress = world.meter.progress();
    CHECK(progress.measured == 0);
    CHECK(progress.missed == 1);
    CHECK(progress.running);
}

TEST_CASE("latency meter: a moved touch, a press near a fading dot and a press beside a held finger are skipped")
{
    World world;
    world.frame(kBackground);
    world.act(TouchAction::Down, 0.5, 0.5);
    world.act(TouchAction::Move, 0.6, 0.5);
    world.frame(kBackground);
    CHECK(world.plannedFresh == 0);
    world.act(TouchAction::Up, 0.6, 0.5);

    world.act(TouchAction::Down, 0.3, 0.3);
    world.act(TouchAction::Up, 0.3, 0.3);
    world.frame(kBackground);
    world.act(TouchAction::Down, 0.3, 0.3);
    world.frame(kBackground);
    CHECK(world.plannedFresh == 0);
    world.act(TouchAction::Up, 0.3, 0.3);

    world.act(TouchAction::Down, 0.8, 0.8, 1);
    world.act(TouchAction::Down, 0.81, 0.8, 2);
    world.frame(kBackground);
    CHECK(world.plannedFresh == 1);
}

TEST_CASE("latency meter: stopping with fewer than three samples gives no result")
{
    World world;
    world.frame(kBackground);
    world.measureOnce(1, 0.2, 0.2);
    world.measureOnce(2, 0.8, 0.8);
    world.meter.finish();
    const auto progress = world.meter.progress();
    CHECK_FALSE(progress.running);
    CHECK(progress.measured == 2);
    CHECK_FALSE(progress.result);
}

TEST_CASE("latency meter: when the dots cannot be seen again and again, it gives up")
{
    LatencyMeterOptions options;
    options.mostMissed = 3;
    World world(options);
    world.frame(kBackground);
    for (int i = 0; i < 3; ++i) {
        CHECK(world.meter.progress().running);
        world.act(TouchAction::Down, 0.2 + 0.3 * i, 0.5);
        world.frames(62, kBackground);
        world.act(TouchAction::Up, 0.2 + 0.3 * i, 0.5);
    }
    const auto progress = world.meter.progress();
    CHECK_FALSE(progress.running);
    CHECK(progress.missed == 3);
    CHECK_FALSE(progress.result);
}
