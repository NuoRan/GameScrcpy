#include "LoadMeter.h"

#include <doctest/doctest.h>

#include <algorithm>

using gs::Microseconds;
using gs::script::Limits;
using gs::script::detail::LoadMeter;

namespace {

Limits meterLimits()
{
    Limits limits;
    limits.spikeThreshold = Microseconds{8'000};
    limits.sustainedWindow = Microseconds{160'000};
    limits.sustainedBusy = Microseconds{80'000};
    return limits;
}

}

TEST_CASE("one long slice is a spike, short slices are not")
{
    LoadMeter meter(meterLimits());
    CHECK_FALSE(meter.record(Microseconds{1'000}, Microseconds{7'999}).spike);
    CHECK(meter.record(Microseconds{20'000}, Microseconds{8'000}).spike);
}

TEST_CASE("busy time over the window threshold warns once until the load drops")
{
    LoadMeter meter(meterLimits());
    int warnings = 0;
    Microseconds now{0};
    for (int i = 0; i < 100; ++i) {
        now += Microseconds{5'000};
        warnings += meter.record(now, Microseconds{4'000}).sustained ? 1 : 0;
    }
    CHECK(warnings == 1);
    for (int i = 0; i < 100; ++i) {
        now += Microseconds{5'000};
        warnings += meter.record(now, Microseconds{100}).sustained ? 1 : 0;
    }
    CHECK(warnings == 1);
    for (int i = 0; i < 100; ++i) {
        now += Microseconds{5'000};
        warnings += meter.record(now, Microseconds{4'000}).sustained ? 1 : 0;
    }
    CHECK(warnings == 2);
}

TEST_CASE("light load spread over time never counts as sustained")
{
    LoadMeter meter(meterLimits());
    Microseconds now{0};
    bool sustained = false;
    Microseconds busiest{0};
    for (int i = 0; i < 1000; ++i) {
        now += Microseconds{5'000};
        const auto verdict = meter.record(now, Microseconds{1'000});
        sustained = sustained || verdict.sustained;
        busiest = std::max(busiest, verdict.busy);
    }
    CHECK_FALSE(sustained);
    CHECK(busiest <= Microseconds{40'000});
}

TEST_CASE("a zero threshold turns the checks off")
{
    Limits limits;
    limits.spikeThreshold = Microseconds{0};
    limits.sustainedBusy = Microseconds{0};
    LoadMeter meter(limits);
    const auto verdict = meter.record(Microseconds{1'000'000}, Microseconds{900'000});
    CHECK_FALSE(verdict.spike);
    CHECK_FALSE(verdict.sustained);
}
