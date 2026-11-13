#include "SendDelayStats.h"

#include <doctest/doctest.h>

using gs::session::detail::SendDelay;
using gs::session::detail::SendDelayStats;

TEST_CASE("ADR-0114: the send delay keeps its mean and its largest for the interval")
{
    SendDelayStats stats;
    const SendDelay empty = stats.take();
    CHECK(empty.meanUs == 0);
    CHECK(empty.maxUs == 0);

    stats.add(300);
    stats.add(100);
    stats.add(200);
    const SendDelay first = stats.take();
    CHECK(first.maxUs == 300);
    CHECK(first.meanUs == 200);

    const SendDelay cleared = stats.take();
    CHECK(cleared.maxUs == 0);
    CHECK(cleared.meanUs == 0);

    stats.add(0);
    stats.add(50);
    const SendDelay after = stats.take();
    CHECK(after.maxUs == 50);
    CHECK(after.meanUs == 25);
}
