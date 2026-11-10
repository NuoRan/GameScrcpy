#include "ReceiverStats.h"

#include <doctest/doctest.h>

#include <cstdint>

using gs::protocol::MediaHeader;
using gs::protocol::MediaKind;
using gs::session::detail::ReceiverStats;

namespace {

MediaHeader header(MediaKind kind, std::uint16_t session, std::int64_t ptsUs = 0, std::uint32_t encodeDelayUs = 0)
{
    MediaHeader result;
    result.kind = kind;
    result.session = session;
    result.ptsUs = ptsUs;
    result.encodeDelayUs = encodeDelayUs;
    return result;
}

}

TEST_CASE("receiver statistics report each interval's frames, bytes and delays once a session is described")
{
    ReceiverStats stats;
    CHECK_FALSE(stats.take(1'000));

    CHECK_FALSE(stats.add(header(MediaKind::Session, 1), 34, 1'000));
    CHECK_FALSE(stats.add(header(MediaKind::Config, 1), 28, 1'100));
    CHECK_FALSE(stats.add(header(MediaKind::Frame, 1, 10'000, 2'000), 5'000, 20'000));
    CHECK_FALSE(stats.add(header(MediaKind::Frame, 1, 26'666, 3'000), 1'200, 40'000));
    CHECK_FALSE(stats.add(header(MediaKind::Frame, 1, 43'333, 2'500), 1'100, 55'000));
    stats.setRoundTrip(1'500);

    const auto first = stats.take(201'000);
    REQUIRE(first);
    CHECK(first->videoSession == 1);
    CHECK(first->intervalUs == 200'000);
    CHECK(first->frames == 3);
    CHECK(first->bytes == 34 + 28 + 5'000 + 1'200 + 1'100);
    CHECK(first->delayMinUs == 8'000);
    CHECK(first->delayMeanUs == 9'167);
    CHECK(first->delayMaxUs == 10'334);
    CHECK(first->roundTripUs == 1'500);
    CHECK(first->lostPackets == 0);
    CHECK(first->recoveredPackets == 0);

    const auto idle = stats.take(401'000);
    REQUIRE(idle);
    CHECK(idle->intervalUs == 200'000);
    CHECK(idle->frames == 0);
    CHECK(idle->bytes == 0);
    CHECK(idle->delayMinUs == 0);
    CHECK(idle->delayMeanUs == 0);
    CHECK(idle->delayMaxUs == 0);
    CHECK(idle->roundTripUs == 1'500);
}

TEST_CASE("receiver statistics close a session's report when a new session is described")
{
    ReceiverStats stats;
    CHECK_FALSE(stats.add(header(MediaKind::Session, 1), 34, 0));
    CHECK_FALSE(stats.add(header(MediaKind::Frame, 1, 0, 5'000), 900, 7'000));

    const auto closed = stats.add(header(MediaKind::Session, 2), 34, 150'000);
    REQUIRE(closed);
    CHECK(closed->videoSession == 1);
    CHECK(closed->intervalUs == 150'000);
    CHECK(closed->frames == 1);
    CHECK(closed->bytes == 34 + 900);
    CHECK(closed->delayMeanUs == 2'000);

    const auto next = stats.take(200'000);
    REQUIRE(next);
    CHECK(next->videoSession == 2);
    CHECK(next->intervalUs == 50'000);
    CHECK(next->frames == 0);
    CHECK(next->bytes == 34);
}

TEST_CASE("receiver statistics start over for a new video connection")
{
    ReceiverStats stats;
    CHECK_FALSE(stats.add(header(MediaKind::Session, 4), 34, 0));
    stats.reset();
    CHECK_FALSE(stats.take(200'000));

    CHECK_FALSE(stats.add(header(MediaKind::Session, 5), 34, 250'000));
    const auto report = stats.take(450'000);
    REQUIRE(report);
    CHECK(report->videoSession == 5);
    CHECK(report->intervalUs == 200'000);
}

TEST_CASE("the reported round trip stays within its field")
{
    ReceiverStats stats;
    CHECK_FALSE(stats.add(header(MediaKind::Session, 1), 34, 0));

    stats.setRoundTrip(-5);
    const auto low = stats.take(1);
    REQUIRE(low);
    CHECK(low->roundTripUs == 0);

    stats.setRoundTrip(5'000'000'000);
    const auto high = stats.take(2);
    REQUIRE(high);
    CHECK(high->roundTripUs == 0xFFFF'FFFFU);
}

TEST_CASE("the shards WiFi direct lost and rebuilt go into the next report, once (SPEC 10.7)")
{
    ReceiverStats stats;
    CHECK_FALSE(stats.add(header(MediaKind::Session, 1), 34, 1'000));
    stats.addLoss(5, 2);
    stats.addLoss(1, 0);
    const auto first = stats.take(201'000);
    REQUIRE(first);
    CHECK(first->lostPackets == 6);
    CHECK(first->recoveredPackets == 2);
    const auto second = stats.take(401'000);
    REQUIRE(second);
    CHECK(second->lostPackets == 0);
    CHECK(second->recoveredPackets == 0);
}
