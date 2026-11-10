#include "gs/base/Clock.h"

#include <doctest/doctest.h>

TEST_CASE("ManualClock moves only when told to")
{
    gs::ManualClock clock{gs::Microseconds{100}};
    CHECK(clock.now().count() == 100);
    clock.advance(gs::Microseconds{250});
    CHECK(clock.now().count() == 350);
    clock.set(gs::Microseconds{5});
    CHECK(clock.now().count() == 5);
}

TEST_CASE("SteadyClock never goes backwards")
{
    const gs::SteadyClock clock;
    auto previous = clock.now().count();
    bool monotonic = true;
    for (int i = 0; i < 10'000; ++i) {
        const auto current = clock.now().count();
        monotonic = monotonic && current >= previous;
        previous = current;
    }
    CHECK(monotonic);
}
