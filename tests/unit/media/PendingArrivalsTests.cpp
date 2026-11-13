#include "PendingArrivals.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

using gs::media::PendingArrivals;

TEST_CASE("ADR-0114: pictures that come out in order carry the arrival of their own frame")
{
    PendingArrivals arrivals;
    arrivals.sent(1'000, 10);
    CHECK(arrivals.held() == 1);
    const std::optional<std::int64_t> first = arrivals.returned(1'000);
    REQUIRE(first);
    CHECK(*first == 10);
    CHECK(arrivals.held() == 0);

    arrivals.sent(2'000, 20);
    arrivals.sent(3'000, 30);
    CHECK(arrivals.held() == 2);
    const std::optional<std::int64_t> second = arrivals.returned(2'000);
    REQUIRE(second);
    CHECK(*second == 20);
    CHECK(arrivals.held() == 1);
    const std::optional<std::int64_t> third = arrivals.returned(3'000);
    REQUIRE(third);
    CHECK(*third == 30);
    CHECK(arrivals.held() == 0);
    CHECK(arrivals.dropped() == 0U);
}

TEST_CASE("ADR-0114: reordered pictures each carry the arrival of their own frame and leave nothing behind")
{
    PendingArrivals arrivals;
    arrivals.sent(3'000, 10);
    arrivals.sent(2'000, 20);
    CHECK(arrivals.held() == 2);
    const std::optional<std::int64_t> shownFirst = arrivals.returned(2'000);
    REQUIRE(shownFirst);
    CHECK(*shownFirst == 20);
    CHECK(arrivals.held() == 1);
    const std::optional<std::int64_t> shownSecond = arrivals.returned(3'000);
    REQUIRE(shownSecond);
    CHECK(*shownSecond == 10);
    CHECK(arrivals.held() == 0);
    CHECK_FALSE(arrivals.returned(2'000));
    CHECK_FALSE(arrivals.returned(3'000));

    const std::array<std::int64_t, 7> decodeOrder{0, 3'000, 1'000, 2'000, 6'000, 4'000, 5'000};
    for (std::size_t i = 0; i < decodeOrder.size(); ++i) {
        CAPTURE(i);
        arrivals.sent(decodeOrder[i], decodeOrder[i] + 50);
        if (i > 0) {
            const std::int64_t shownPts = static_cast<std::int64_t>(i - 1) * 1'000;
            const std::optional<std::int64_t> arrival = arrivals.returned(shownPts);
            REQUIRE(arrival);
            CHECK(*arrival == shownPts + 50);
        }
        CHECK(arrivals.held() == 1);
    }
    const std::optional<std::int64_t> last = arrivals.returned(6'000);
    REQUIRE(last);
    CHECK(*last == 6'050);
    CHECK(arrivals.held() == 0);
}

TEST_CASE("ADR-0114: a frame whose picture never comes out is dropped once a later picture does")
{
    PendingArrivals arrivals;
    arrivals.sent(1'000, 10);
    arrivals.sent(2'000, 20);
    arrivals.sent(3'000, 30);
    const std::optional<std::int64_t> arrival = arrivals.returned(3'000);
    REQUIRE(arrival);
    CHECK(*arrival == 30);
    CHECK(arrivals.held() == 0);
    CHECK_FALSE(arrivals.returned(1'000));
    CHECK_FALSE(arrivals.returned(2'000));
}

TEST_CASE("ADR-0114: the record keeps at most 32 frames and counts the oldest ones it drops")
{
    PendingArrivals arrivals;
    for (std::int64_t i = 0; i < 40; ++i) {
        arrivals.sent(i * 1'000, i);
    }
    CHECK(arrivals.held() == 32);
    CHECK(arrivals.dropped() == 8U);
    CHECK_FALSE(arrivals.returned(7'000));
    CHECK(arrivals.held() == 32);
    const std::optional<std::int64_t> oldestKept = arrivals.returned(8'000);
    REQUIRE(oldestKept);
    CHECK(*oldestKept == 8);
    CHECK(arrivals.held() == 31);
    const std::optional<std::int64_t> newest = arrivals.returned(39'000);
    REQUIRE(newest);
    CHECK(*newest == 39);
    CHECK(arrivals.held() == 0);
    CHECK(arrivals.dropped() == 8U);
}

TEST_CASE("ADR-0114: a picture whose pts matches no frame leaves the record as it was")
{
    PendingArrivals arrivals;
    arrivals.sent(1'000, 10);
    arrivals.sent(2'000, 20);
    CHECK_FALSE(arrivals.returned(5'000));
    CHECK(arrivals.held() == 2);
    const std::optional<std::int64_t> arrival = arrivals.returned(1'000);
    REQUIRE(arrival);
    CHECK(*arrival == 10);
    CHECK(arrivals.held() == 1);
}

TEST_CASE("ADR-0114: clearing the record forgets every frame and the count of dropped ones")
{
    PendingArrivals arrivals;
    for (std::int64_t i = 0; i < 33; ++i) {
        arrivals.sent(i, i);
    }
    REQUIRE(arrivals.dropped() == 1U);
    arrivals.clear();
    CHECK(arrivals.held() == 0);
    CHECK(arrivals.dropped() == 0U);
    CHECK_FALSE(arrivals.returned(20));
    arrivals.sent(40, 400);
    const std::optional<std::int64_t> arrival = arrivals.returned(40);
    REQUIRE(arrival);
    CHECK(*arrival == 400);
}

TEST_CASE("ADR-0114: a frame the decoder refused is forgotten without touching the others")
{
    PendingArrivals arrivals;
    arrivals.sent(1'000, 10);
    arrivals.sent(2'000, 20);
    arrivals.forget(2'000);
    CHECK(arrivals.held() == 1);
    arrivals.forget(9'000);
    CHECK(arrivals.held() == 1);
    const std::optional<std::int64_t> arrival = arrivals.returned(1'000);
    REQUIRE(arrival);
    CHECK(*arrival == 10);
    CHECK(arrivals.held() == 0);

    arrivals.sent(4'000, 40);
    arrivals.sent(4'000, 41);
    arrivals.forget(4'000);
    CHECK(arrivals.held() == 1);
    const std::optional<std::int64_t> kept = arrivals.returned(4'000);
    REQUIRE(kept);
    CHECK(*kept == 40);
}
