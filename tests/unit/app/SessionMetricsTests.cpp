#include "SessionMetrics.h"

#include <gs/session/Session.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>

using gs::app::SessionMetrics;
using gs::session::Heartbeat;
using namespace std::chrono_literals;

namespace {

Heartbeat heartbeat(std::chrono::microseconds roundTrip, std::chrono::microseconds clockOffset,
                    std::chrono::microseconds sendDelay, std::int64_t sentUs = 0, std::int64_t serverTimeUs = 0)
{
    Heartbeat beat;
    beat.roundTrip = roundTrip;
    beat.clockOffset = clockOffset;
    beat.sendDelay = sendDelay;
    beat.sentUs = sentUs;
    beat.serverTimeUs = serverTimeUs;
    return beat;
}

}

TEST_CASE("session metrics give rates and means per sample, and latency once the clock offset is known")
{
    SessionMetrics metrics;

    metrics.videoFrame(10'000, 1'000'000, 12'000, 2'000'000, 1'500);
    const SessionMetrics::Sample first = metrics.sample(1s);
    CHECK(first.framesPerSecond == doctest::Approx(1));
    CHECK(first.megabitsPerSecond == doctest::Approx(0.08));
    REQUIRE(first.decodeMs);
    CHECK(*first.decodeMs == doctest::Approx(1.5));
    REQUIRE(first.encodeMs);
    CHECK(*first.encodeMs == doctest::Approx(12));
    CHECK_FALSE(first.captureToReceiptMs);
    CHECK_FALSE(first.networkMs);
    CHECK_FALSE(first.roundTripMs);

    metrics.heartbeat(heartbeat(1'200us, 5s, 300us));
    for (std::int64_t i = 0; i < 30; ++i) {
        const std::int64_t receivedUs = 10'000'000 + i * 33'333;
        metrics.videoFrame(25'000, receivedUs - 40'000 + 5'000'000, 15'000, receivedUs, 2'000);
    }
    const SessionMetrics::Sample second = metrics.sample(500ms);
    CHECK(second.framesPerSecond == doctest::Approx(60));
    CHECK(second.megabitsPerSecond == doctest::Approx(12));
    REQUIRE(second.decodeMs);
    CHECK(*second.decodeMs == doctest::Approx(2));
    REQUIRE(second.encodeMs);
    CHECK(*second.encodeMs == doctest::Approx(15));
    REQUIRE(second.captureToReceiptMs);
    CHECK(*second.captureToReceiptMs == doctest::Approx(40));
    REQUIRE(second.networkMs);
    CHECK(*second.networkMs == doctest::Approx(25));
    REQUIRE(second.roundTripMs);
    CHECK(*second.roundTripMs == doctest::Approx(1.2));
    REQUIRE(second.sendDelayMs);
    CHECK(*second.sendDelayMs == doctest::Approx(0.3));

    const SessionMetrics::Sample third = metrics.sample(500ms);
    CHECK(third.framesPerSecond == 0);
    CHECK_FALSE(third.decodeMs);
    CHECK_FALSE(third.encodeMs);
    CHECK_FALSE(third.captureToReceiptMs);
    CHECK_FALSE(third.networkMs);
    CHECK(third.roundTripMs);
    CHECK_FALSE(third.inputMs);
}

TEST_CASE("session metrics time an input reaching the phone and its wait for the next captured frame")
{
    constexpr std::int64_t kFrameUs = 16'666;
    constexpr std::int64_t kOffsetUs = 5'000'000;
    const auto injectedAt = [&](SessionMetrics& metrics, std::int64_t phoneUs) {
        metrics.heartbeat(heartbeat(3ms, 5s, 0us, phoneUs - kOffsetUs - 1'500, phoneUs));
    };
    const auto frame = [](SessionMetrics& metrics, std::int64_t ptsUs) { metrics.videoFrame(1'000, ptsUs, 0, 0, 0); };

    SessionMetrics metrics;
    for (std::int64_t i = 0; i < 3; ++i) {
        frame(metrics, 6'000'000 + i * kFrameUs);
    }
    injectedAt(metrics, 6'010'000);
    injectedAt(metrics, 6'040'000);
    frame(metrics, 6'000'000 + 3 * kFrameUs);
    injectedAt(metrics, 6'060'000);
    frame(metrics, 6'200'000);

    const SessionMetrics::Sample sample = metrics.sample(1s);
    REQUIRE(sample.inputMs);
    CHECK(*sample.inputMs == doctest::Approx(1.5));
    REQUIRE(sample.frameWaitMs);
    CHECK(*sample.frameWaitMs == doctest::Approx((6'666 + 9'998) / 2.0 / 1000.0));

    SessionMetrics still;
    for (std::int64_t i = 0; i < 5; ++i) {
        frame(still, 9'000'000 + i * kFrameUs);
    }
    const SessionMetrics::Sample estimated = still.sample(1s);
    CHECK_FALSE(estimated.inputMs);
    REQUIRE(estimated.frameWaitMs);
    CHECK(*estimated.frameWaitMs == doctest::Approx(kFrameUs / 2.0 / 1000.0));

    SessionMetrics idle;
    frame(idle, 1'000'000);
    frame(idle, 2'000'000);
    CHECK_FALSE(idle.sample(1s).frameWaitMs);
}
