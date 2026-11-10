#include "gs/input/Controls.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

using namespace gs::input;
using gs::Microseconds;
using gs::protocol::TouchAction;
using gs::protocol::TouchFrame;
using gs::protocol::TouchPoint;

namespace {

constexpr Microseconds ms(long long value)
{
    return Microseconds{value * 1000};
}

std::uint16_t fx(double normalized)
{
    return static_cast<std::uint16_t>(std::lround(std::clamp(normalized, 0.0, 1.0) * 65535.0));
}

class RecordingSink final : public TouchFrameSink
{
public:
    std::vector<TouchFrame> frames;

    bool trySend(const TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }
};

struct Rig {
    PointerAllocator pointers;
    TouchFrameBuilder builder{Microseconds{0}};
    TouchOutput out{pointers, builder};
    RecordingSink sink;

    template <typename Control>
    void settle(Control& control, Microseconds now)
    {
        builder.flush(now, sink);
        for (int round = 0; round < 32; ++round) {
            const auto due = control.nextDeadline();
            if (!due || *due > now) {
                return;
            }
            control.tick(now, out);
            builder.flush(now, sink);
        }
        FAIL("the control keeps asking for follow-up rounds");
    }

    std::vector<TouchPoint> last() const
    {
        REQUIRE_FALSE(sink.frames.empty());
        return {sink.frames.back().points.begin(), sink.frames.back().points.end()};
    }
};

TouchPoint at(TouchAction action, std::uint8_t id, double x, double y)
{
    const bool lifting = action == TouchAction::Up || action == TouchAction::Cancel;
    return {.action = action, .pointerId = id, .x = fx(x), .y = fx(y),
            .pressure = lifting ? std::uint16_t{0} : std::uint16_t{0xFFFF}};
}

}

TEST_CASE("a held button keeps its finger down until released (MAP-002)")
{
    Rig rig;
    ButtonComponent button{{.position = {0.8, 0.7}}};
    button.signal(Slot::Press, TriggerSignal::Activate, ms(0), rig.out);
    rig.settle(button, ms(0));
    CHECK(rig.last() == std::vector{at(TouchAction::Down, 0, 0.8, 0.7)});
    rig.settle(button, ms(2000));
    CHECK(rig.sink.frames.size() == 1);
    button.signal(Slot::Press, TriggerSignal::Deactivate, ms(2000), rig.out);
    rig.settle(button, ms(2000));
    CHECK(rig.last() == std::vector{at(TouchAction::Up, 0, 0.8, 0.7)});
}

TEST_CASE("a tap presses and lifts after the tap duration, exactly on target without jitter (MAP-001)")
{
    Rig rig;
    ButtonComponent button{{.position = {0.25, 0.5}, .tapDuration = ms(40)}};
    button.signal(Slot::Press, TriggerSignal::Pulse, ms(0), rig.out);
    rig.settle(button, ms(0));
    CHECK(button.nextDeadline() == ms(40));
    rig.settle(button, ms(39));
    CHECK(rig.sink.frames.size() == 1);
    rig.settle(button, ms(40));
    REQUIRE(rig.sink.frames.size() == 2);
    CHECK(rig.last() == std::vector{at(TouchAction::Up, 0, 0.25, 0.5)});
}

TEST_CASE("turbo taps at its rate while held and stops on release (MAP-003)")
{
    Rig rig;
    ButtonComponent button{{.position = {0.5, 0.5}, .tapDuration = ms(33)}};
    TriggerState turbo{TriggerConfig{.trigger = Trigger::Turbo, .rateHz = 10}};
    std::vector<Microseconds> pressedAt;
    const auto deliver = [&](TriggerSignal signal, Microseconds now) {
        if (signal == TriggerSignal::Pulse) {
            pressedAt.push_back(now);
        }
        button.signal(Slot::Press, signal, now, rig.out);
    };
    deliver(turbo.press(ms(0)), ms(0));
    for (long long t = 1; t <= 400; ++t) {
        if (t == 250) {
            deliver(turbo.release(ms(t)), ms(t));
        }
        deliver(turbo.tick(ms(t)), ms(t));
        button.tick(ms(t), rig.out);
        rig.builder.flush(ms(t), rig.sink);
    }
    CHECK(pressedAt == std::vector{ms(0), ms(100), ms(200)});
    const auto downs = std::count_if(rig.sink.frames.begin(), rig.sink.frames.end(), [](const TouchFrame& f) {
        return f.points[0].action == TouchAction::Down;
    });
    const auto ups = std::count_if(rig.sink.frames.begin(), rig.sink.frames.end(), [](const TouchFrame& f) {
        return f.points[0].action == TouchAction::Up;
    });
    CHECK(downs == 3);
    CHECK(ups == 3);
    CHECK(rig.pointers.activeCount() == 0);
}

TEST_CASE("the joystick lands at its centre, moves in the next round and keeps diagonals on the circle (MAP-005)")
{
    const Vec2 centre{0.2, 0.7};
    const Vec2 radius{0.0675, 0.12};
    Rig rig;
    JoystickComponent stick{{.center = centre, .radius = radius}};

    stick.signal(Slot::Up, TriggerSignal::Activate, ms(0), rig.out);
    rig.settle(stick, ms(0));
    REQUIRE(rig.sink.frames.size() == 2);
    CHECK(rig.sink.frames[0].points[0] == at(TouchAction::Down, 0, 0.2, 0.7));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.2, 0.7 - 0.12)});

    stick.signal(Slot::Right, TriggerSignal::Activate, ms(10), rig.out);
    rig.settle(stick, ms(10));
    const double d = 1.0 / std::sqrt(2.0);
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.2 + 0.0675 * d, 0.7 - 0.12 * d)});
    CHECK(std::hypot(0.0675 * d * 1920, 0.12 * d * 1080) == doctest::Approx(0.12 * 1080));

    stick.signal(Slot::Walk, TriggerSignal::Activate, ms(20), rig.out);
    rig.settle(stick, ms(20));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.2 + 0.0675 * d * 0.5, 0.7 - 0.12 * d * 0.5)});

    stick.signal(Slot::Walk, TriggerSignal::Deactivate, ms(30), rig.out);
    stick.signal(Slot::Up, TriggerSignal::Deactivate, ms(30), rig.out);
    stick.signal(Slot::Sprint, TriggerSignal::Activate, ms(30), rig.out);
    rig.settle(stick, ms(30));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.2 + 0.0675 * 1.3, 0.7)});

    stick.signal(Slot::Right, TriggerSignal::Deactivate, ms(40), rig.out);
    rig.settle(stick, ms(40));
    CHECK(rig.last() == std::vector{at(TouchAction::Up, 0, 0.2 + 0.0675 * 1.3, 0.7)});
    CHECK(rig.pointers.activeCount() == 0);
}

TEST_CASE("all eight key directions point within 1 degree of their ideal angle in pixels (MAP-015)")
{
    struct Direction {
        Slot first;
        std::optional<Slot> second;
        double degrees;
    };
    const Direction directions[] = {
        {Slot::Right, {}, 0.0},        {Slot::Right, Slot::Down, 45.0}, {Slot::Down, {}, 90.0},
        {Slot::Down, Slot::Left, 135.0}, {Slot::Left, {}, 180.0},       {Slot::Left, Slot::Up, -135.0},
        {Slot::Up, {}, -90.0},         {Slot::Up, Slot::Right, -45.0},
    };
    for (const Direction& direction : directions) {
        INFO(direction.degrees);
        Rig rig;
        JoystickComponent stick{{.center = {0.5, 0.5}, .radius = {0.0675, 0.12}}};
        stick.signal(direction.first, TriggerSignal::Activate, ms(0), rig.out);
        if (direction.second) {
            stick.signal(*direction.second, TriggerSignal::Activate, ms(0), rig.out);
        }
        rig.settle(stick, ms(0));
        const TouchPoint point = rig.last().at(0);
        const double dx = (point.x / 65535.0 - 0.5) * 1920.0;
        const double dy = (point.y / 65535.0 - 0.5) * 1080.0;
        double error = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846 - direction.degrees;
        error = std::remainder(error, 360.0);
        CHECK(std::abs(error) < 1.0);
        CHECK(std::hypot(dx, dy) == doctest::Approx(0.12 * 1080).epsilon(0.002));
    }
}

TEST_CASE("an analog stick deflects the joystick proportionally (MAP-015)")
{
    Rig rig;
    JoystickComponent stick{{.center = {0.2, 0.7}, .radius = {0.0675, 0.12}}};
    stick.axis(Slot::Stick, {0.5, 0.0}, ms(0), rig.out);
    rig.settle(stick, ms(0));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.2 + 0.0675 * 0.5, 0.7)});
    stick.axis(Slot::Stick, {3.0, 4.0}, ms(10), rig.out);
    rig.settle(stick, ms(10));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.2 + 0.0675 * 0.6, 0.7 + 0.12 * 0.8)});
    stick.axis(Slot::Stick, {0.0, 0.0}, ms(20), rig.out);
    rig.settle(stick, ms(20));
    CHECK(rig.last()[0].action == TouchAction::Up);
}

TEST_CASE("a joystick lands on its center and goes straight to the target (ADR-0073)")
{
    Rig rig;
    JoystickComponent stick{{.center = {0.5, 0.5}, .radius = {0.1, 0.1}}};
    stick.signal(Slot::Right, TriggerSignal::Activate, ms(0), rig.out);
    rig.settle(stick, ms(0));
    REQUIRE(rig.sink.frames.size() == 2);
    CHECK(rig.sink.frames[0].points[0] == at(TouchAction::Down, 0, 0.5, 0.5));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.6, 0.5)});
    CHECK_FALSE(stick.nextDeadline().has_value());
    stick.signal(Slot::Up, TriggerSignal::Activate, ms(5), rig.out);
    rig.settle(stick, ms(5));
    const double diagonal = 0.1 / std::sqrt(2.0);
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.5 + diagonal, 0.5 - diagonal)});
    CHECK_FALSE(stick.nextDeadline().has_value());
}

TEST_CASE("the camera hands over to a second finger at the edge without losing motion (MAP-006)")
{
    Rig rig;
    CameraComponent camera{{.anchor = {0.6, 0.4}, .halfExtent = {0.1, 0.1}, .perCount = {1e-4, 1e-4}}};

    camera.motion(Slot::Look, 500, 0, ms(0), rig.out);
    rig.settle(camera, ms(0));
    REQUIRE(rig.sink.frames.size() == 2);
    CHECK(rig.sink.frames[0].points[0] == at(TouchAction::Down, 0, 0.6, 0.4));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.65, 0.4)});

    camera.motion(Slot::Look, 400, 0, ms(10), rig.out);
    rig.settle(camera, ms(10));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.69, 0.4)});

    camera.motion(Slot::Look, 500, 0, ms(20), rig.out);
    rig.settle(camera, ms(20));
    REQUIRE(rig.sink.frames.size() == 6);
    CHECK(std::vector<TouchPoint>(rig.sink.frames[3].points.begin(), rig.sink.frames[3].points.end()) ==
          std::vector{at(TouchAction::Move, 0, 0.70, 0.4), at(TouchAction::Down, 1, 0.6, 0.4)});
    CHECK(rig.sink.frames[4].points[0] == at(TouchAction::Up, 0, 0.70, 0.4));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 1, 0.64, 0.4)});
}

TEST_CASE("a flick far beyond the area hands over repeatedly and keeps the whole motion (MAP-006)")
{
    Rig rig;
    CameraComponent camera{{.anchor = {0.5, 0.5}, .halfExtent = {0.1, 0.1}, .perCount = {1e-4, 1e-4}}};
    camera.motion(Slot::Look, 5000, -2500, ms(0), rig.out);
    rig.settle(camera, ms(0));

    std::map<std::uint8_t, std::pair<TouchPoint, TouchPoint>> travel;
    double totalX = 0.0;
    double totalY = 0.0;
    for (const TouchFrame& frame : rig.sink.frames) {
        for (const TouchPoint& point : frame.points) {
            if (point.action == TouchAction::Down) {
                travel[point.pointerId] = {point, point};
            } else {
                travel[point.pointerId].second = point;
            }
            if (point.action == TouchAction::Up || point.action == TouchAction::Cancel) {
                const auto& [first, last] = travel[point.pointerId];
                totalX += (last.x - first.x) / 65535.0;
                totalY += (last.y - first.y) / 65535.0;
                travel.erase(point.pointerId);
            }
        }
    }
    for (const auto& [id, span] : travel) {
        totalX += (span.second.x - span.first.x) / 65535.0;
        totalY += (span.second.y - span.first.y) / 65535.0;
    }
    CHECK(totalX == doctest::Approx(0.5).epsilon(0.001));
    CHECK(totalY == doctest::Approx(-0.25).epsilon(0.001));
    CHECK(rig.pointers.activeCount() == 1);
}

TEST_CASE("an idle camera hands over to a finger at the anchor and keeps touching, like v1")
{
    Rig rig;
    CameraComponent camera{{.anchor = {0.6, 0.4}, .halfExtent = {0.1, 0.1}, .perCount = {1e-4, 1e-4},
                            .idleRecenter = ms(1000)}};
    camera.motion(Slot::Look, 300, 0, ms(0), rig.out);
    rig.settle(camera, ms(0));
    CHECK(camera.nextDeadline() == ms(1000));
    const std::size_t before = rig.sink.frames.size();
    rig.settle(camera, ms(1000));
    REQUIRE(rig.sink.frames.size() == before + 2);
    CHECK(rig.sink.frames[before].points[0] == at(TouchAction::Down, 1, 0.6, 0.4));
    CHECK(rig.last() == std::vector{at(TouchAction::Up, 0, 0.63, 0.4)});
    CHECK(rig.pointers.activeCount() == 1);
    CHECK_FALSE(camera.nextDeadline().has_value());
    camera.motion(Slot::Look, 100, 0, ms(1500), rig.out);
    rig.settle(camera, ms(1500));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 1, 0.61, 0.4)});
}

TEST_CASE("over the whole picture the finger slides past the area and hands over only near the edge, like v1")
{
    Rig rig;
    CameraComponent camera{{.anchor = {0.6, 0.4}, .halfExtent = {0.1, 0.1}, .perCount = {1e-4, 1e-4},
                            .wholePicture = true}};
    camera.motion(Slot::Look, 1500, 0, ms(0), rig.out);
    rig.settle(camera, ms(0));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.75, 0.4)});
    camera.motion(Slot::Look, 2500, 0, ms(10), rig.out);
    rig.settle(camera, ms(10));
    const auto downs = std::count_if(rig.sink.frames.begin(), rig.sink.frames.end(), [](const TouchFrame& frame) {
        return std::ranges::any_of(frame.points, [](const TouchPoint& p) { return p.action == TouchAction::Down; });
    });
    CHECK(downs == 2);
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 1, 0.62, 0.4)});
}

TEST_CASE("reset cancels every finger of every control (MAP-011)")
{
    Rig rig;
    ButtonComponent button{{.position = {0.8, 0.8}}};
    JoystickComponent stick{{.center = {0.2, 0.7}, .radius = {0.07, 0.12}}};
    CameraComponent camera{{.anchor = {0.6, 0.4}, .halfExtent = {0.1, 0.1}, .perCount = {1e-4, 1e-4}}};
    button.signal(Slot::Press, TriggerSignal::Activate, ms(0), rig.out);
    stick.signal(Slot::Left, TriggerSignal::Activate, ms(0), rig.out);
    camera.motion(Slot::Look, 2500, 0, ms(0), rig.out);
    rig.settle(stick, ms(0));
    rig.settle(camera, ms(0));
    CHECK(rig.pointers.activeCount() >= 3);

    button.reset(rig.out);
    stick.reset(rig.out);
    camera.reset(rig.out);
    rig.builder.flush(ms(1), rig.sink);
    CHECK(rig.pointers.activeCount() == 0);
    for (const TouchPoint& point : rig.last()) {
        CHECK(point.action == TouchAction::Cancel);
    }
    stick.signal(Slot::Up, TriggerSignal::Activate, ms(10), rig.out);
    rig.settle(stick, ms(10));
    const TouchPoint moved = rig.last().at(0);
    CHECK(moved.action == TouchAction::Move);
    CHECK(moved.x == fx(0.2));
    CHECK(moved.y == fx(0.7 - 0.12));
}

TEST_CASE("free look drives its own finger only while held and recentres on release (MAP-007)")
{
    Rig rig;
    FreeLookComponent peek{{.position = {0.8, 0.3}, .perCount = {1e-4, 1e-4}}};
    peek.motion(Slot::Look, 1000, 0, ms(0), rig.out);
    rig.settle(peek, ms(0));
    CHECK(rig.sink.frames.empty());

    peek.signal(Slot::Hold, TriggerSignal::Activate, ms(10), rig.out);
    CHECK(peek.engaged());
    peek.motion(Slot::Look, 300, 200, ms(10), rig.out);
    rig.settle(peek, ms(10));
    REQUIRE(rig.sink.frames.size() == 2);
    CHECK(rig.sink.frames[0].points[0] == at(TouchAction::Down, 0, 0.8, 0.3));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.83, 0.32)});

    peek.motion(Slot::Look, -300, 0, ms(20), rig.out);
    rig.settle(peek, ms(20));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.80, 0.32)});

    peek.signal(Slot::Hold, TriggerSignal::Deactivate, ms(30), rig.out);
    rig.settle(peek, ms(30));
    CHECK(rig.last() == std::vector{at(TouchAction::Up, 0, 0.80, 0.32)});
    CHECK_FALSE(peek.engaged());
}

TEST_CASE("free look without recentring keeps the view until the next press")
{
    Rig rig;
    FreeLookComponent peek{{.position = {0.8, 0.3}, .perCount = {1e-4, 1e-4}, .resetOnRelease = false}};
    peek.signal(Slot::Hold, TriggerSignal::Activate, ms(0), rig.out);
    rig.settle(peek, ms(0));
    peek.signal(Slot::Hold, TriggerSignal::Deactivate, ms(100), rig.out);
    rig.settle(peek, ms(100));
    CHECK(rig.sink.frames.size() == 1);
    CHECK(rig.pointers.activeCount() == 1);
    peek.signal(Slot::Hold, TriggerSignal::Activate, ms(500), rig.out);
    rig.settle(peek, ms(500));
    CHECK(rig.last()[0].action == TouchAction::Up);
    CHECK(rig.pointers.activeCount() == 0);
}

TEST_CASE("aiming drags inside the radius and casts or cancels on release (MAP-008)")
{
    Rig rig;
    AimComponent aim{{.position = {0.7, 0.6}, .radius = {0.05, 0.1}, .perCount = {1e-4, 1e-4}}};
    aim.signal(Slot::Press, TriggerSignal::Activate, ms(0), rig.out);
    rig.settle(aim, ms(0));
    CHECK(rig.last() == std::vector{at(TouchAction::Down, 0, 0.7, 0.6)});

    aim.motion(Slot::Look, 0, -500, ms(10), rig.out);
    rig.settle(aim, ms(10));
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.7, 0.55)});

    aim.motion(Slot::Look, 5000, 0, ms(20), rig.out);
    rig.settle(aim, ms(20));
    const TouchPoint clamped = rig.last().at(0);
    const double ux = (clamped.x / 65535.0 - 0.7) / 0.05;
    const double uy = (clamped.y / 65535.0 - 0.6) / 0.1;
    CHECK(ux * ux + uy * uy == doctest::Approx(1.0).epsilon(0.001));

    aim.signal(Slot::Press, TriggerSignal::Deactivate, ms(30), rig.out);
    rig.settle(aim, ms(30));
    CHECK(rig.last()[0].action == TouchAction::Up);

    AimComponent hesitant{{.position = {0.7, 0.6}, .radius = {0.05, 0.1}, .perCount = {1e-4, 1e-4},
                           .castOnRelease = false}};
    hesitant.signal(Slot::Press, TriggerSignal::Activate, ms(100), rig.out);
    rig.settle(hesitant, ms(100));
    hesitant.signal(Slot::Press, TriggerSignal::Deactivate, ms(200), rig.out);
    rig.settle(hesitant, ms(200));
    CHECK(rig.last()[0].action == TouchAction::Cancel);
    CHECK(rig.pointers.activeCount() == 0);
}

TEST_CASE("a swipe follows its path at uniform speed and lifts at the end")
{
    Rig rig;
    SwipeComponent swipe{{.path = {{0.5, 0.5}, {0.5, 0.4}, {0.8, 0.4}}, .duration = ms(200), .frame = ms(10)}};
    swipe.signal(Slot::Press, TriggerSignal::Pulse, ms(0), rig.out);
    rig.settle(swipe, ms(0));
    CHECK(rig.last() == std::vector{at(TouchAction::Down, 0, 0.5, 0.5)});
    for (long long t = 10; t <= 100; t += 10) {
        rig.settle(swipe, ms(t));
    }
    CHECK(rig.last() == std::vector{at(TouchAction::Move, 0, 0.6, 0.4)});
    for (long long t = 110; t <= 200; t += 10) {
        rig.settle(swipe, ms(t));
    }
    CHECK(rig.last() == std::vector{at(TouchAction::Up, 0, 0.8, 0.4)});
    CHECK_FALSE(swipe.nextDeadline().has_value());
}

TEST_CASE("pinch gestures move two fingers symmetrically and lift both (MAP-016)")
{
    Rig rig;
    PinchComponent pinch{{.center = {0.3, 0.3}, .halfSpan = 0.05, .halfStep = 0.02, .duration = ms(100),
                          .frame = ms(20)}};
    pinch.signal(Slot::ZoomIn, TriggerSignal::Pulse, ms(0), rig.out);
    rig.settle(pinch, ms(0));
    CHECK(rig.last() == std::vector{at(TouchAction::Down, 0, 0.25, 0.3), at(TouchAction::Down, 1, 0.35, 0.3)});
    for (long long t = 20; t <= 100; t += 20) {
        rig.settle(pinch, ms(t));
        const std::vector<TouchPoint> points = rig.last();
        REQUIRE(points.size() == 2);
        CHECK(points[0].x + points[1].x == doctest::Approx(2 * fx(0.3)).epsilon(0.0001));
    }
    CHECK(rig.last() == std::vector{at(TouchAction::Up, 0, 0.23, 0.3), at(TouchAction::Up, 1, 0.37, 0.3)});

    pinch.signal(Slot::ZoomOut, TriggerSignal::Pulse, ms(200), rig.out);
    for (long long t = 200; t <= 300; t += 20) {
        rig.settle(pinch, ms(t));
    }
    const std::vector<TouchPoint> released = rig.last();
    CHECK(released[0] == at(TouchAction::Up, released[0].pointerId, 0.27, 0.3));
    CHECK(released[1] == at(TouchAction::Up, released[1].pointerId, 0.33, 0.3));
    CHECK(rig.pointers.activeCount() == 0);
}