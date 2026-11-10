#include "gs/input/TouchFrameBuilder.h"

#include <doctest/doctest.h>

#include <vector>

using namespace gs::input;
using gs::Microseconds;
using gs::protocol::TouchAction;
using gs::protocol::TouchFrame;
using gs::protocol::TouchPoint;

namespace {

constexpr Microseconds kTick{4166};

struct Sent {
    Microseconds at;
    TouchFrame frame;
};

class RecordingSink final : public TouchFrameSink
{
public:
    bool writable = true;
    Microseconds now{0};
    std::vector<Sent> sent;

    bool trySend(const TouchFrame& frame) override
    {
        if (writable) {
            sent.push_back({now, frame});
        }
        return writable;
    }
};

struct Fixture {
    TouchFrameBuilder builder{kTick};
    RecordingSink sink;

    void flushAt(Microseconds now)
    {
        sink.now = now;
        builder.flush(now, sink);
    }
};

TouchPoint point(TouchAction action, std::uint8_t id, std::uint16_t x, std::uint16_t y)
{
    const bool lifting = action == TouchAction::Up || action == TouchAction::Cancel;
    return TouchPoint{.action = action, .pointerId = id, .x = x, .y = y,
                      .pressure = lifting ? std::uint16_t{0} : std::uint16_t{0xFFFF}};
}

std::vector<TouchPoint> points(const TouchFrame& frame)
{
    return {frame.points.begin(), frame.points.end()};
}

}

TEST_CASE("presses go out at once; moves go out on the leading edge, then once per tick (MAP-017)")
{
    Fixture f;
    f.builder.down(0, {0.5, 0.5});
    f.flushAt(Microseconds{0});
    REQUIRE(f.sink.sent.size() == 1);
    CHECK(points(f.sink.sent[0].frame) == std::vector{point(TouchAction::Down, 0, 32768, 32768)});

    f.builder.move(0, {0.6, 0.5});
    f.flushAt(Microseconds{1000});
    f.builder.move(0, {0.7, 0.5});
    f.flushAt(Microseconds{2000});
    CHECK(f.sink.sent.size() == 1);
    CHECK(f.builder.nextWakeup() == kTick);

    f.flushAt(kTick);
    REQUIRE(f.sink.sent.size() == 2);
    CHECK(points(f.sink.sent[1].frame) == std::vector{point(TouchAction::Move, 0, 45875, 32768)});
    CHECK_FALSE(f.builder.nextWakeup().has_value());

    f.builder.move(0, {0.8, 0.5});
    f.flushAt(Microseconds{50000});
    REQUIRE(f.sink.sent.size() == 3);
    CHECK(f.sink.sent[2].at == Microseconds{50000});
}

TEST_CASE("pointers changing in the same tick share one frame")
{
    Fixture f;
    f.builder.down(0, {0.1, 0.9});
    f.builder.down(1, {0.7, 0.3});
    f.flushAt(Microseconds{0});
    f.builder.move(0, {0.15, 0.85});
    f.builder.move(1, {0.72, 0.31});
    f.flushAt(kTick);
    REQUIRE(f.sink.sent.size() == 2);
    CHECK(f.sink.sent[0].frame.points.size() == 2);
    CHECK(f.sink.sent[1].frame.points.size() == 2);
}

TEST_CASE("a release carries the pending move of its own pointer and goes out at once")
{
    Fixture f;
    f.builder.down(0, {0.2, 0.2});
    f.builder.down(1, {0.4, 0.4});
    f.flushAt(Microseconds{0});
    f.builder.move(1, {0.5, 0.5});
    f.builder.move(0, {0.3, 0.3});
    f.builder.up(0, {0.3, 0.3});
    f.flushAt(Microseconds{100});
    REQUIRE(f.sink.sent.size() == 2);
    CHECK(points(f.sink.sent[1].frame) ==
          std::vector{point(TouchAction::Move, 1, 32768, 32768), point(TouchAction::Up, 0, 19661, 19661)});
}

TEST_CASE("changes that cannot merge close the frame first")
{
    Fixture f;
    f.builder.down(3, {0.5, 0.5});
    f.builder.up(3, {0.5, 0.5});
    f.flushAt(Microseconds{0});
    REQUIRE(f.sink.sent.size() == 2);
    CHECK(f.sink.sent[0].frame.points[0].action == TouchAction::Down);
    CHECK(f.sink.sent[1].frame.points[0].action == TouchAction::Up);
}

TEST_CASE("a congested link keeps only the latest positions and loses no press or release")
{
    Fixture f;
    f.builder.down(0, {0.5, 0.5});
    f.flushAt(Microseconds{0});

    f.sink.writable = false;
    for (int i = 1; i <= 50; ++i) {
        f.builder.move(0, {0.5 + 0.005 * i, 0.5});
        f.flushAt(kTick * i);
    }
    f.builder.down(1, {0.1, 0.1});
    f.flushAt(kTick * 51);
    f.builder.up(1, {0.1, 0.1});
    f.flushAt(kTick * 52);
    CHECK_FALSE(f.builder.nextWakeup().has_value());
    CHECK(f.sink.sent.size() == 1);

    f.sink.writable = true;
    f.flushAt(kTick * 53);
    REQUIRE(f.sink.sent.size() == 3);
    CHECK(points(f.sink.sent[1].frame) ==
          std::vector{point(TouchAction::Move, 0, 49151, 32768), point(TouchAction::Down, 1, 6554, 6554)});
    CHECK(points(f.sink.sent[2].frame) == std::vector{point(TouchAction::Up, 1, 6554, 6554)});
    CHECK(f.builder.idle());
}

TEST_CASE("a zero tick sends every move immediately and positions are clamped to the screen")
{
    TouchFrameBuilder builder{Microseconds{0}};
    RecordingSink sink;
    builder.down(0, {-0.2, 1.5});
    builder.flush(Microseconds{0}, sink);
    builder.move(0, {1.0, 0.0});
    builder.flush(Microseconds{1}, sink);
    REQUIRE(sink.sent.size() == 2);
    CHECK(points(sink.sent[0].frame) == std::vector{point(TouchAction::Down, 0, 0, 65535)});
    CHECK(points(sink.sent[1].frame) == std::vector{point(TouchAction::Move, 0, 65535, 0)});
}

TEST_CASE("frames carry the current video session")
{
    Fixture f;
    f.builder.setVideoSession(7);
    f.builder.down(0, {0.5, 0.5});
    f.flushAt(Microseconds{0});
    REQUIRE(f.sink.sent.size() == 1);
    CHECK(f.sink.sent[0].frame.videoSession == 7);
}
