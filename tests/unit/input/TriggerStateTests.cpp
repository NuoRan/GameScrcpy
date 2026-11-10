#include "gs/input/TriggerState.h"

#include <doctest/doctest.h>

using namespace gs::input;
using gs::Microseconds;

namespace {

constexpr Microseconds ms(long long value)
{
    return Microseconds{value * 1000};
}

TriggerState make(Trigger trigger)
{
    return TriggerState{TriggerConfig{.trigger = trigger, .longPressMs = 300, .doubleTapMs = 250, .rateHz = 10}};
}

}

TEST_CASE("hold activates on press and deactivates on release (MAP-013)")
{
    TriggerState state = make(Trigger::Hold);
    CHECK(state.press(ms(0)) == TriggerSignal::Activate);
    CHECK(state.press(ms(5)) == TriggerSignal::None);
    CHECK(state.active());
    CHECK(state.release(ms(40)) == TriggerSignal::Deactivate);
    CHECK_FALSE(state.nextDeadline().has_value());
}

TEST_CASE("tap pulses on press regardless of how long the input is held")
{
    TriggerState state = make(Trigger::Tap);
    CHECK(state.press(ms(0)) == TriggerSignal::Pulse);
    CHECK(state.release(ms(900)) == TriggerSignal::None);
}

TEST_CASE("a tap sharing its input with a long press is decided on release")
{
    TriggerState state{TriggerConfig{.trigger = Trigger::Tap}, ms(300)};
    CHECK(state.press(ms(0)) == TriggerSignal::None);
    CHECK(state.release(ms(120)) == TriggerSignal::Pulse);
    CHECK(state.press(ms(1000)) == TriggerSignal::None);
    CHECK(state.release(ms(1400)) == TriggerSignal::None);
}

TEST_CASE("long press activates once the threshold is reached and holds until release")
{
    TriggerState state = make(Trigger::LongPress);
    CHECK(state.press(ms(0)) == TriggerSignal::None);
    CHECK(state.nextDeadline() == ms(300));
    CHECK(state.tick(ms(299)) == TriggerSignal::None);
    CHECK(state.tick(ms(300)) == TriggerSignal::Activate);
    CHECK_FALSE(state.nextDeadline().has_value());
    CHECK(state.release(ms(500)) == TriggerSignal::Deactivate);

    CHECK(state.press(ms(1000)) == TriggerSignal::None);
    CHECK(state.release(ms(1100)) == TriggerSignal::None);
    CHECK(state.tick(ms(1400)) == TriggerSignal::None);
}

TEST_CASE("double tap activates on a second press inside the window")
{
    TriggerState state = make(Trigger::DoubleTap);
    CHECK(state.press(ms(0)) == TriggerSignal::None);
    CHECK(state.release(ms(60)) == TriggerSignal::None);
    CHECK(state.press(ms(200)) == TriggerSignal::Activate);
    CHECK(state.release(ms(260)) == TriggerSignal::Deactivate);

    CHECK(state.press(ms(1000)) == TriggerSignal::None);
    CHECK(state.release(ms(1050)) == TriggerSignal::None);
    CHECK(state.tick(ms(1250)) == TriggerSignal::None);
    CHECK_FALSE(state.nextDeadline().has_value());
    CHECK(state.press(ms(1300)) == TriggerSignal::None);
}

TEST_CASE("toggle flips on every press")
{
    TriggerState state = make(Trigger::Toggle);
    CHECK(state.press(ms(0)) == TriggerSignal::Activate);
    CHECK(state.release(ms(50)) == TriggerSignal::None);
    CHECK(state.press(ms(500)) == TriggerSignal::Deactivate);
    CHECK_FALSE(state.active());
}

TEST_CASE("turbo pulses at once, then at its rate, and stops on release")
{
    TriggerState state = make(Trigger::Turbo);
    CHECK(state.press(ms(0)) == TriggerSignal::Pulse);
    CHECK(state.nextDeadline() == ms(100));
    CHECK(state.tick(ms(100)) == TriggerSignal::Pulse);
    CHECK(state.nextDeadline() == ms(200));
    CHECK(state.tick(ms(450)) == TriggerSignal::Pulse);
    CHECK(state.nextDeadline() == ms(550));
    CHECK(state.release(ms(500)) == TriggerSignal::None);
    CHECK_FALSE(state.nextDeadline().has_value());
}

TEST_CASE("release pulses when the input goes up")
{
    TriggerState state = make(Trigger::Release);
    CHECK(state.press(ms(0)) == TriggerSignal::None);
    CHECK(state.release(ms(80)) == TriggerSignal::Pulse);
    CHECK(state.release(ms(90)) == TriggerSignal::None);
}

TEST_CASE("reset returns to idle without a signal")
{
    TriggerState state = make(Trigger::LongPress);
    CHECK(state.press(ms(0)) == TriggerSignal::None);
    state.reset();
    CHECK_FALSE(state.nextDeadline().has_value());
    CHECK(state.release(ms(10)) == TriggerSignal::None);
}
