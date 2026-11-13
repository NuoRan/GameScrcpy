#include "SessionMetrics.h"

#include <gs/session/Session.h>

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>

using gs::app::SessionMetrics;
using gs::session::Heartbeat;
using gs::session::VideoStall;
using namespace std::chrono_literals;

namespace {

Heartbeat heartbeat(std::chrono::microseconds roundTrip, std::chrono::microseconds clockOffset,
                    std::chrono::microseconds sendDelay, std::int64_t sentUs = 0, std::int64_t serverTimeUs = 0)
{
    Heartbeat beat;
    beat.roundTrip = roundTrip;
    beat.clockOffset = clockOffset;
    beat.sendDelay = sendDelay;
    beat.sendDelayMean = sendDelay;
    beat.sentUs = sentUs;
    beat.serverTimeUs = serverTimeUs;
    return beat;
}

Heartbeat beatAt(std::int64_t sentUs, std::chrono::microseconds roundTrip, std::chrono::microseconds oneWay)
{
    return heartbeat(roundTrip, 0us, 0us, sentUs, sentUs + oneWay.count());
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
    REQUIRE(second.sendDelayMeanMs);
    CHECK(*second.sendDelayMeanMs == doctest::Approx(0.3));

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

TEST_CASE("ADR-0114: session metrics give the most frames the decoder held back during each sample")
{
    SessionMetrics metrics;
    CHECK(metrics.sample(1s).heldFrames == 0);

    metrics.decoderHolds(0);
    metrics.decoderHolds(2);
    metrics.decoderHolds(1);
    CHECK(metrics.sample(1s).heldFrames == 2);
    CHECK(metrics.sample(1s).heldFrames == 0);

    metrics.decoderHolds(1);
    metrics.decoderHolds(0);
    CHECK(metrics.sample(500ms).heldFrames == 1);
}

TEST_CASE("ADR-0114: session metrics count the pauses for a key frame, the longest and the packets lost, per sample")
{
    SessionMetrics metrics;
    const SessionMetrics::Sample calm = metrics.sample(1s);
    CHECK(calm.stalls == 0);
    CHECK(calm.longestStallMs == 0);
    CHECK(calm.lostPackets == 0);

    metrics.videoStall(VideoStall{.waited = 120ms, .lostPackets = 3, .keyFrameRequests = 1});
    metrics.videoStall(VideoStall{.waited = 340ms, .keyFrameRequests = 1});
    metrics.videoStall(VideoStall{.waited = 80ms, .lostPackets = 5, .keyFrameRequests = 3});
    const SessionMetrics::Sample paused = metrics.sample(1s);
    CHECK(paused.stalls == 3);
    CHECK(paused.longestStallMs == doctest::Approx(340));
    CHECK(paused.lostPackets == 8);

    const SessionMetrics::Sample after = metrics.sample(1s);
    CHECK(after.stalls == 0);
    CHECK(after.longestStallMs == 0);
    CHECK(after.lostPackets == 0);
}

TEST_CASE("ADR-0114: the round trip and the one-way estimate show the latest sample and the best of 16 s")
{
    SessionMetrics metrics;
    CHECK_FALSE(metrics.sample(1s).roundTripBestMs);

    metrics.heartbeat(beatAt(1, 8ms, 6ms));
    metrics.heartbeat(beatAt(1'000'000, 4ms, 3ms));
    metrics.heartbeat(beatAt(2'000'000, 6ms, 5ms));
    const SessionMetrics::Sample playing = metrics.sample(1s);
    REQUIRE(playing.roundTripMs);
    REQUIRE(playing.roundTripBestMs);
    REQUIRE(playing.inputMs);
    REQUIRE(playing.inputBestMs);
    CHECK(*playing.roundTripMs == doctest::Approx(6));
    CHECK(*playing.roundTripBestMs == doctest::Approx(4));
    CHECK(*playing.inputMs == doctest::Approx(5));
    CHECK(*playing.inputBestMs == doctest::Approx(3));
    CHECK(*playing.inputMs <= *playing.roundTripMs);

    metrics.heartbeat(beatAt(19'000'000, 10ms, 7ms));
    const SessionMetrics::Sample later = metrics.sample(1s);
    REQUIRE(later.roundTripMs);
    REQUIRE(later.roundTripBestMs);
    REQUIRE(later.inputMs);
    REQUIRE(later.inputBestMs);
    CHECK(*later.roundTripMs == doctest::Approx(10));
    CHECK(*later.roundTripBestMs == doctest::Approx(10));
    CHECK(*later.inputMs == doctest::Approx(7));
    CHECK(*later.inputBestMs == doctest::Approx(7));
}

TEST_CASE("ADR-0114: a one-way estimate is clamped to its own round trip and never negative")
{
    SessionMetrics metrics;
    metrics.heartbeat(beatAt(1'000'000, 4ms, 6ms));
    const SessionMetrics::Sample clamped = metrics.sample(1s);
    REQUIRE(clamped.inputMs);
    REQUIRE(clamped.roundTripMs);
    CHECK(*clamped.inputMs == doctest::Approx(4));
    CHECK(*clamped.inputBestMs == doctest::Approx(4));
    CHECK(*clamped.roundTripBestMs == doctest::Approx(4));

    SessionMetrics ahead;
    ahead.heartbeat(beatAt(1'000'000, 4ms, -2ms));
    const SessionMetrics::Sample zero = ahead.sample(1s);
    REQUIRE(zero.inputMs);
    CHECK(*zero.inputMs == doctest::Approx(0));
}

TEST_CASE("ADR-0114: the heartbeat history keeps at most 64 samples inside the 16 s window")
{
    SessionMetrics metrics;
    metrics.heartbeat(beatAt(1, 20ms, 15ms));
    for (int i = 1; i < 100; ++i) {
        metrics.heartbeat(beatAt(1 + i * 10'000, 5ms, 4ms));
    }
    const SessionMetrics::Sample sample = metrics.sample(1s);
    REQUIRE(sample.roundTripBestMs);
    REQUIRE(sample.inputBestMs);
    CHECK(*sample.roundTripBestMs == doctest::Approx(5));
    CHECK(*sample.inputBestMs == doctest::Approx(4));
    CHECK(*sample.roundTripMs == doctest::Approx(5));
    CHECK(*sample.inputMs == doctest::Approx(4));
}

TEST_CASE("ADR-0114: the touch wait of one interval shows its mean and its largest, and nothing when idle")
{
    SessionMetrics metrics;
    const SessionMetrics::Sample idle = metrics.sample(1s);
    CHECK_FALSE(idle.mergeWaitMeanMs);
    CHECK_FALSE(idle.mergeWaitMaxMs);

    metrics.touchWait(9'000, 3, 4'000);
    const SessionMetrics::Sample touched = metrics.sample(1s);
    REQUIRE(touched.mergeWaitMeanMs);
    REQUIRE(touched.mergeWaitMaxMs);
    CHECK(*touched.mergeWaitMeanMs == doctest::Approx(3));
    CHECK(*touched.mergeWaitMaxMs == doctest::Approx(4));

    const SessionMetrics::Sample after = metrics.sample(1s);
    CHECK_FALSE(after.mergeWaitMeanMs);
    CHECK_FALSE(after.mergeWaitMaxMs);
}
