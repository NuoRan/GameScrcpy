#include "KeyFrameGate.h"

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>

using namespace gs;
using gs::session::detail::KeyFrameGate;

namespace {

constexpr std::int64_t kRoundTripUs = 10'000;
constexpr std::int64_t kFrameUs = 16'666;

protocol::MediaHeader frame(bool key, std::int64_t ptsUs = 0)
{
    protocol::MediaHeader header;
    header.kind = protocol::MediaKind::Frame;
    header.keyFrame = key;
    header.session = 1;
    header.ptsUs = ptsUs;
    return header;
}

protocol::MediaHeader description()
{
    protocol::MediaHeader header;
    header.kind = protocol::MediaKind::Session;
    header.session = 2;
    return header;
}

protocol::MediaHeader configuration()
{
    protocol::MediaHeader header;
    header.kind = protocol::MediaKind::Config;
    header.session = 2;
    return header;
}

struct Retry {
    bool before = false;
    bool at = false;
};

Retry retryAround(std::int64_t roundTripUs, std::int64_t gapUs, std::int64_t expectedUs)
{
    KeyFrameGate gate;
    (void)gate.admit(frame(false, 0), 0, 0, roundTripUs);
    (void)gate.admit(frame(false, gapUs), 1, 1'000'000, roundTripUs);
    Retry retry;
    retry.before = gate.admit(frame(false, gapUs + kFrameUs), 0, 1'000'000 + expectedUs - 1, roundTripUs).askKeyFrame;
    retry.at = gate.admit(frame(false, gapUs + 2 * kFrameUs), 0, 1'000'000 + expectedUs, roundTripUs).askKeyFrame;
    return retry;
}

}

TEST_CASE("after a lost packet the frames wait for a key frame, which is asked for (SPEC 10.5, WIF-003)")
{
    KeyFrameGate gate;
    auto decision = gate.admit(frame(false), 0, 0, kRoundTripUs);
    CHECK((decision.deliver && !decision.askKeyFrame));

    decision = gate.admit(frame(false), 2, 1'000, kRoundTripUs);
    CHECK_FALSE(decision.deliver);
    CHECK(decision.askKeyFrame);
    CHECK(gate.waiting());
    CHECK_FALSE(gate.admit(frame(false), 0, 2'000, kRoundTripUs).deliver);
    CHECK(gate.admit(description(), 0, 3'000, kRoundTripUs).deliver);

    decision = gate.admit(frame(true), 0, 4'000, kRoundTripUs);
    CHECK(decision.deliver);
    CHECK_FALSE(decision.askKeyFrame);
    CHECK_FALSE(gate.waiting());
    CHECK(gate.admit(frame(false), 0, 5'000, kRoundTripUs).deliver);
}

TEST_CASE("ADR-0114: a key frame that does not come is asked for again, each time after twice as long, at most 500 ms")
{
    KeyFrameGate gate;
    CHECK(gate.admit(frame(false, 0), 0, 1'000'000, kRoundTripUs).deliver);
    auto decision = gate.admit(frame(false, kFrameUs), 1, 1'016'666, kRoundTripUs);
    CHECK(decision.askKeyFrame);
    CHECK_FALSE(decision.deliver);

    constexpr std::array<std::int64_t, 5> intervals{100'000, 200'000, 400'000, 500'000, 500'000};
    std::int64_t askedUs = 1'016'666;
    std::int64_t ptsUs = kFrameUs;
    for (const std::int64_t intervalUs : intervals) {
        CAPTURE(intervalUs);
        ptsUs += kFrameUs;
        CHECK_FALSE(gate.admit(frame(false, ptsUs), 0, askedUs + intervalUs - 1, kRoundTripUs).askKeyFrame);
        ptsUs += kFrameUs;
        decision = gate.admit(frame(false, ptsUs), 0, askedUs + intervalUs, kRoundTripUs);
        CHECK(decision.askKeyFrame);
        CHECK_FALSE(decision.deliver);
        askedUs += intervalUs;
    }
    CHECK(gate.waiting());
}

TEST_CASE("ADR-0114: the first retry comes after three round trips and two frames, at least 100 ms and at most 500 ms")
{
    const Retry usual = retryAround(10'000, 16'666, 100'000);
    CHECK_FALSE(usual.before);
    CHECK(usual.at);

    const Retry shortest = retryAround(1'000, 8'333, 100'000);
    CHECK_FALSE(shortest.before);
    CHECK(shortest.at);

    const Retry longest = retryAround(400'000, 16'666, 500'000);
    CHECK_FALSE(longest.before);
    CHECK(longest.at);

    const Retry slowFrames = retryAround(10'000, 100'000, 230'000);
    CHECK_FALSE(slowFrames.before);
    CHECK(slowFrames.at);

    const Retry slowLink = retryAround(30'000, 16'666, 123'332);
    CHECK_FALSE(slowLink.before);
    CHECK(slowLink.at);

    const Retry samePts = retryAround(40'000, 0, 120'000);
    CHECK_FALSE(samePts.before);
    CHECK(samePts.at);
}

TEST_CASE("ADR-0114: an unknown round trip counts as 100 ms")
{
    const Retry unknown = retryAround(0, 16'666, 333'332);
    CHECK_FALSE(unknown.before);
    CHECK(unknown.at);
}

TEST_CASE("ADR-0114: a frame gap outside 0 to 100 ms counts as 1/60 s")
{
    const Retry paused = retryAround(30'000, 150'000, 123'332);
    CHECK_FALSE(paused.before);
    CHECK(paused.at);

    const Retry backwards = retryAround(30'000, -5'000, 123'332);
    CHECK_FALSE(backwards.before);
    CHECK(backwards.at);

    KeyFrameGate gate;
    CHECK(gate.admit(frame(false, 0), 1, 1'000'000, kRoundTripUs).askKeyFrame);
    CHECK_FALSE(gate.admit(frame(false, kFrameUs), 0, 1'099'999, kRoundTripUs).askKeyFrame);
    CHECK(gate.admit(frame(false, 2 * kFrameUs), 0, 1'100'000, kRoundTripUs).askKeyFrame);
}

TEST_CASE("ADR-0114: a key frame ends the wait, and the next loss asks at once and starts from the first interval")
{
    KeyFrameGate gate;
    CHECK(gate.admit(frame(false, 0), 0, 1'000'000, kRoundTripUs).deliver);
    CHECK(gate.admit(frame(false, kFrameUs), 1, 1'016'666, kRoundTripUs).askKeyFrame);
    CHECK(gate.admit(frame(false, 2 * kFrameUs), 0, 1'116'666, kRoundTripUs).askKeyFrame);
    CHECK(gate.admit(frame(false, 3 * kFrameUs), 0, 1'316'666, kRoundTripUs).askKeyFrame);
    const auto key = gate.admit(frame(true, 4 * kFrameUs), 0, 1'350'000, kRoundTripUs);
    CHECK(key.deliver);
    CHECK_FALSE(key.askKeyFrame);
    CHECK_FALSE(gate.waiting());
    CHECK(gate.admit(frame(false, 5 * kFrameUs), 0, 1'366'666, kRoundTripUs).deliver);

    const auto lost = gate.admit(frame(false, 6 * kFrameUs), 2, 1'383'332, kRoundTripUs);
    CHECK(lost.askKeyFrame);
    CHECK_FALSE(lost.deliver);
    CHECK_FALSE(gate.admit(frame(false, 7 * kFrameUs), 0, 1'383'332 + 99'999, kRoundTripUs).askKeyFrame);
    CHECK(gate.admit(frame(false, 8 * kFrameUs), 0, 1'383'332 + 100'000, kRoundTripUs).askKeyFrame);
}

TEST_CASE("ADR-0114: while waiting, frames are held back, other packets pass, and any packet can ask again")
{
    KeyFrameGate gate;
    CHECK(gate.admit(frame(false, 0), 0, 1'000'000, kRoundTripUs).deliver);
    CHECK(gate.admit(frame(false, kFrameUs), 3, 1'016'666, kRoundTripUs).askKeyFrame);
    auto decision = gate.admit(frame(false, 2 * kFrameUs), 2, 1'033'332, kRoundTripUs);
    CHECK_FALSE(decision.deliver);
    CHECK_FALSE(decision.askKeyFrame);
    decision = gate.admit(description(), 0, 1'040'000, kRoundTripUs);
    CHECK(decision.deliver);
    CHECK_FALSE(decision.askKeyFrame);
    decision = gate.admit(configuration(), 0, 1'116'666, kRoundTripUs);
    CHECK(decision.deliver);
    CHECK(decision.askKeyFrame);
    CHECK(gate.waiting());
    CHECK_FALSE(gate.admit(frame(false, 3 * kFrameUs), 0, 1'120'000, kRoundTripUs).deliver);
    CHECK(gate.admit(frame(true, 4 * kFrameUs), 0, 1'130'000, kRoundTripUs).deliver);
    CHECK_FALSE(gate.waiting());
}

TEST_CASE("ADR-0114: a key frame right after a loss needs no wait and no request")
{
    KeyFrameGate gate;
    CHECK(gate.admit(frame(false, 0), 0, 1'000'000, kRoundTripUs).deliver);
    const auto decision = gate.admit(frame(true, kFrameUs), 4, 1'016'666, kRoundTripUs);
    CHECK(decision.deliver);
    CHECK_FALSE(decision.askKeyFrame);
    CHECK_FALSE(decision.stall);
    CHECK_FALSE(gate.waiting());
    CHECK(gate.admit(frame(false, 2 * kFrameUs), 0, 1'033'332, kRoundTripUs).deliver);
}

TEST_CASE("ADR-0114: the key frame that ends a wait reports how long it lasted, the packets lost and the requests")
{
    KeyFrameGate gate;
    CHECK_FALSE(gate.admit(frame(false, 0), 0, 1'000'000, kRoundTripUs).stall);
    auto decision = gate.admit(frame(false, kFrameUs), 2, 1'016'666, kRoundTripUs);
    CHECK(decision.askKeyFrame);
    CHECK_FALSE(decision.stall);
    CHECK(gate.admit(frame(false, 2 * kFrameUs), 0, 1'116'666, kRoundTripUs).askKeyFrame);
    decision = gate.admit(frame(false, 3 * kFrameUs), 4, 1'120'000, kRoundTripUs);
    CHECK_FALSE(decision.askKeyFrame);
    CHECK_FALSE(decision.stall);
    CHECK_FALSE(gate.admit(configuration(), 0, 1'130'000, kRoundTripUs).stall);

    decision = gate.admit(frame(true, 4 * kFrameUs), 1, 1'166'666, kRoundTripUs);
    CHECK(decision.deliver);
    CHECK_FALSE(decision.askKeyFrame);
    REQUIRE(decision.stall);
    CHECK(decision.stall->waited == std::chrono::microseconds(150'000));
    CHECK(decision.stall->lostPackets == 7);
    CHECK(decision.stall->keyFrameRequests == 2);
    CHECK_FALSE(gate.admit(frame(false, 5 * kFrameUs), 0, 1'183'332, kRoundTripUs).stall);
    CHECK_FALSE(gate.admit(frame(true, 6 * kFrameUs), 0, 1'200'000, kRoundTripUs).stall);

    CHECK(gate.admit(frame(false, 7 * kFrameUs), 5, 1'216'666, kRoundTripUs).askKeyFrame);
    decision = gate.admit(frame(true, 8 * kFrameUs), 0, 1'233'332, kRoundTripUs);
    REQUIRE(decision.stall);
    CHECK(decision.stall->waited == std::chrono::microseconds(16'666));
    CHECK(decision.stall->lostPackets == 5);
    CHECK(decision.stall->keyFrameRequests == 1);

    KeyFrameGate flooded;
    CHECK(flooded.admit(frame(false, 0), 0xFFFF'FFFFU, 1'000, kRoundTripUs).askKeyFrame);
    CHECK_FALSE(flooded.admit(frame(false, kFrameUs), 0xFFFF'FFFFU, 2'000, kRoundTripUs).deliver);
    decision = flooded.admit(frame(true, 2 * kFrameUs), 1, 3'000, kRoundTripUs);
    REQUIRE(decision.stall);
    CHECK(decision.stall->lostPackets == 0xFFFF'FFFFU);
}
