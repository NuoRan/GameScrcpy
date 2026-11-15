#include "PointerMerge.h"

#include <gs/base/Clock.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

using gs::ManualClock;
using gs::Microseconds;
using gs::app::MouseStep;
using gs::app::PointerMerge;

namespace {

constexpr Microseconds kTick{8'000};

}

TEST_CASE("R9: mouse motion is summed and leaves at most once per pacing tick, and what is left is kept")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    CHECK_FALSE(merge.take().has_value());
    CHECK_FALSE(merge.busy());

    merge.addMotion(3, 4);
    const std::optional<MouseStep> first = merge.take();
    REQUIRE(first.has_value());
    CHECK(first->dx == 3);
    CHECK(first->dy == 4);
    CHECK(first->buttons == 0U);

    merge.addMotion(2, 2);
    CHECK_FALSE(merge.take().has_value());
    clock.advance(kTick);
    merge.addMotion(1, 0);
    const std::optional<MouseStep> second = merge.take();
    REQUIRE(second.has_value());
    CHECK(second->dx == 3);
    CHECK(second->dy == 2);
    CHECK_FALSE(merge.busy());
}

TEST_CASE("R9: a move larger than one report carries is split over the next ticks, never lost")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.addMotion(40'000, -40'000);
    const std::optional<MouseStep> first = merge.take();
    REQUIRE(first.has_value());
    CHECK(first->dx == 32767);
    CHECK(first->dy == -32767);
    CHECK(merge.busy());

    clock.advance(kTick);
    const std::optional<MouseStep> second = merge.take();
    REQUIRE(second.has_value());
    CHECK(second->dx == 7233);
    CHECK(second->dy == -7233);
    CHECK_FALSE(merge.busy());
}

TEST_CASE("R9: the wheel is summed too and leaves in whole notches at the tick rate")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.addMotion(0, 0, 200);
    const std::optional<MouseStep> first = merge.take();
    REQUIRE(first.has_value());
    CHECK(first->wheel == 127);
    CHECK(first->dx == 0);
    clock.advance(kTick);
    const std::optional<MouseStep> second = merge.take();
    REQUIRE(second.has_value());
    CHECK(second->wheel == 73);
}

TEST_CASE("R9: a change of buttons sends the motion it was holding first, then the buttons, at once")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.addMotion(5, -2);
    merge.setButtons(1);
    const std::optional<MouseStep> motion = merge.take();
    REQUIRE(motion.has_value());
    CHECK(motion->dx == 5);
    CHECK(motion->dy == -2);
    CHECK(motion->buttons == 0U);
    const std::optional<MouseStep> pressed = merge.take();
    REQUIRE(pressed.has_value());
    CHECK(pressed->dx == 0);
    CHECK(pressed->dy == 0);
    CHECK(pressed->buttons == 1U);
    CHECK_FALSE(merge.busy());

    merge.setButtons(1);
    CHECK_FALSE(merge.take().has_value());
    merge.setButtons(0);
    const std::optional<MouseStep> released = merge.take();
    REQUIRE(released.has_value());
    CHECK(released->buttons == 0U);
}

TEST_CASE("R9: nothing is sent when there is nothing to send")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.addMotion(0, 0, 0);
    merge.setButtons(0);
    CHECK_FALSE(merge.take().has_value());
    CHECK_FALSE(merge.busy());
    clock.advance(kTick);
    CHECK_FALSE(merge.take().has_value());
}

TEST_CASE("R9: the phone's pointer is put back under the computer's pointer from the corner after the pause")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.recenter(700, 1500);
    const std::optional<MouseStep> corner = merge.take();
    REQUIRE(corner.has_value());
    CHECK(corner->dx == -32767);
    CHECK(corner->dy == -32767);
    CHECK(merge.holding());
    CHECK_FALSE(merge.take().has_value());

    merge.addMotion(10, 0);
    clock.advance(kTick);
    CHECK_FALSE(merge.take().has_value());
    CHECK(merge.holding());
    clock.advance(gs::app::kRecenterPause - kTick - Microseconds{1});
    CHECK_FALSE(merge.take().has_value());
    clock.advance(Microseconds{1});
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(back->dx == 710);
    CHECK(back->dy == 1500);
    CHECK_FALSE(merge.holding());
    CHECK_FALSE(merge.busy());
}

TEST_CASE("R9: a new unlock or turn restarts the way back, and stopping drops it")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.recenter(100, 200);
    REQUIRE(merge.take().has_value());
    CHECK(merge.holding());
    merge.recenter(300, 400);
    const std::optional<MouseStep> corner = merge.take();
    REQUIRE(corner.has_value());
    CHECK(corner->dx == -32767);
    clock.advance(gs::app::kRecenterPause);
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(back->dx == 300);
    CHECK(back->dy == 400);

    merge.recenter(50, 60);
    REQUIRE(merge.take().has_value());
    merge.addMotion(7, 7);
    merge.setButtons(2);
    merge.reset();
    CHECK_FALSE(merge.busy());
    CHECK_FALSE(merge.take().has_value());
    CHECK(merge.buttons() == 0U);
}

TEST_CASE("R9: a press and a release while the pointer waits in the corner reach the phone only after the step back, "
          "in order")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.recenter(500, 500);
    REQUIRE(merge.take().has_value());
    merge.setButtons(1);
    CHECK_FALSE(merge.take().has_value());
    clock.advance(kTick);
    merge.setButtons(0);
    CHECK_FALSE(merge.take().has_value());
    CHECK(merge.holding());
    CHECK(merge.buttons() == 0U);

    clock.advance(gs::app::kRecenterPause);
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(*back == MouseStep{.dx = 500, .dy = 500, .wheel = 0, .buttons = 0});
    const std::optional<MouseStep> pressed = merge.take();
    REQUIRE(pressed.has_value());
    CHECK(*pressed == MouseStep{.dx = 0, .dy = 0, .wheel = 0, .buttons = 1});
    const std::optional<MouseStep> released = merge.take();
    REQUIRE(released.has_value());
    CHECK(*released == MouseStep{.dx = 0, .dy = 0, .wheel = 0, .buttons = 0});
    CHECK_FALSE(merge.take().has_value());
    CHECK_FALSE(merge.busy());
}

TEST_CASE("R9: outside the wait in the corner, buttons go out as before: the motion first, then the change, at once")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.recenter(20, 30);
    REQUIRE(merge.take().has_value());
    clock.advance(gs::app::kRecenterPause);
    REQUIRE(merge.take().has_value());
    CHECK_FALSE(merge.holding());

    merge.addMotion(4, -1);
    merge.setButtons(2);
    const std::optional<MouseStep> motion = merge.take();
    REQUIRE(motion.has_value());
    CHECK(*motion == MouseStep{.dx = 4, .dy = -1, .wheel = 0, .buttons = 0});
    const std::optional<MouseStep> pressed = merge.take();
    REQUIRE(pressed.has_value());
    CHECK(*pressed == MouseStep{.dx = 0, .dy = 0, .wheel = 0, .buttons = 2});
    merge.setButtons(0);
    const std::optional<MouseStep> released = merge.take();
    REQUIRE(released.has_value());
    CHECK(*released == MouseStep{.dx = 0, .dy = 0, .wheel = 0, .buttons = 0});
    CHECK_FALSE(merge.take().has_value());
}

TEST_CASE("R9: more than 8 button changes while the pointer waits in the corner keep the first 7 and the last")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.recenter(10, 10);
    REQUIRE(merge.take().has_value());
    for (std::uint32_t state = 1; state <= 10; ++state) {
        merge.setButtons(state);
    }
    CHECK_FALSE(merge.take().has_value());
    clock.advance(gs::app::kRecenterPause);
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(*back == MouseStep{.dx = 10, .dy = 10, .wheel = 0, .buttons = 0});
    std::vector<std::uint32_t> sent;
    while (const std::optional<MouseStep> step = merge.take()) {
        CHECK(step->dx == 0);
        CHECK(step->dy == 0);
        sent.push_back(step->buttons);
    }
    CHECK(sent == std::vector<std::uint32_t>{1, 2, 3, 4, 5, 6, 7, 10});
    CHECK(gs::app::kMostHeldButtons == 8U);
}

TEST_CASE("R9: motion still waiting when the pointer is put back is already in the target and is not added again")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.addMotion(1, 1);
    REQUIRE(merge.take().has_value());
    merge.addMotion(6, 0);
    merge.recenter(100, 100);
    REQUIRE(merge.take().has_value());
    clock.advance(gs::app::kRecenterPause);
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(*back == MouseStep{.dx = 100, .dy = 100, .wheel = 0, .buttons = 0});
    CHECK_FALSE(merge.busy());
}

TEST_CASE("R9: a park sends the corner step at once, without waiting for the tick, with the buttons the phone has")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.setButtons(4);
    REQUIRE(merge.take().has_value());
    merge.addMotion(9, 9, 2);
    merge.park();
    const std::optional<MouseStep> corner = merge.take();
    REQUIRE(corner.has_value());
    CHECK(*corner == MouseStep{.dx = -32767, .dy = -32767, .wheel = 0, .buttons = 4});
    CHECK(merge.holding());
    CHECK_FALSE(merge.take().has_value());
    CHECK_FALSE(merge.busy());
}

TEST_CASE("R9: after a park, a recenter once the pause is over sends only the step to the target, at once")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.park();
    const std::optional<MouseStep> corner = merge.take();
    REQUIRE(corner.has_value());
    CHECK(*corner == MouseStep{.dx = -32767, .dy = -32767, .wheel = 0, .buttons = 0});

    merge.addMotion(5, 5);
    clock.advance(gs::app::kRecenterPause);
    CHECK_FALSE(merge.take().has_value());
    CHECK_FALSE(merge.busy());
    merge.recenter(400, 900);
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(*back == MouseStep{.dx = 400, .dy = 900, .wheel = 0, .buttons = 0});
    CHECK_FALSE(merge.holding());
    CHECK_FALSE(merge.take().has_value());
    CHECK_FALSE(merge.busy());
}

TEST_CASE("R9: after a park, a recenter before the pause is over waits only for the rest and sends no second corner")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.park();
    REQUIRE(merge.take().has_value());
    constexpr Microseconds kEarly{200'000};
    clock.advance(kEarly);
    merge.recenter(400, 900);
    CHECK_FALSE(merge.take().has_value());
    CHECK(merge.busy());
    merge.addMotion(3, -3);
    clock.advance(gs::app::kRecenterPause - kEarly - Microseconds{1});
    CHECK_FALSE(merge.take().has_value());
    clock.advance(Microseconds{1});
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(*back == MouseStep{.dx = 403, .dy = 897, .wheel = 0, .buttons = 0});
    CHECK_FALSE(merge.take().has_value());
}

TEST_CASE("R9: a click while the parked pointer waits for the rest of the pause reaches the phone after the step back")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.park();
    REQUIRE(merge.take().has_value());
    clock.advance(Microseconds{100'000});
    merge.recenter(300, 600);
    merge.setButtons(1);
    merge.setButtons(0);
    CHECK_FALSE(merge.take().has_value());
    clock.advance(gs::app::kRecenterPause);
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(*back == MouseStep{.dx = 300, .dy = 600, .wheel = 0, .buttons = 0});
    const std::optional<MouseStep> pressed = merge.take();
    REQUIRE(pressed.has_value());
    CHECK(*pressed == MouseStep{.dx = 0, .dy = 0, .wheel = 0, .buttons = 1});
    const std::optional<MouseStep> released = merge.take();
    REQUIRE(released.has_value());
    CHECK(*released == MouseStep{.dx = 0, .dy = 0, .wheel = 0, .buttons = 0});
    CHECK_FALSE(merge.take().has_value());
}

TEST_CASE("R9: a park keeps only the last button state it was holding, and stopping clears the park")
{
    ManualClock clock;
    PointerMerge merge(kTick, clock);
    merge.recenter(50, 50);
    REQUIRE(merge.take().has_value());
    merge.setButtons(1);
    merge.setButtons(0);
    merge.park();
    const std::optional<MouseStep> corner = merge.take();
    REQUIRE(corner.has_value());
    CHECK(corner->buttons == 0U);
    clock.advance(gs::app::kRecenterPause);
    merge.recenter(70, 80);
    const std::optional<MouseStep> back = merge.take();
    REQUIRE(back.has_value());
    CHECK(*back == MouseStep{.dx = 70, .dy = 80, .wheel = 0, .buttons = 0});
    CHECK_FALSE(merge.take().has_value());

    merge.park();
    REQUIRE(merge.take().has_value());
    merge.reset();
    CHECK_FALSE(merge.holding());
    merge.recenter(10, 20);
    const std::optional<MouseStep> again = merge.take();
    REQUIRE(again.has_value());
    CHECK(again->dx == -32767);
    CHECK(again->dy == -32767);
}
