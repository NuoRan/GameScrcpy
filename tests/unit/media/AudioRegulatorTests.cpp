#include "gs/media/AudioRegulator.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using gs::media::AudioRegulator;
using gs::media::Compensation;

namespace {

constexpr int kRate = 48'000;
constexpr int kChannels = 2;
constexpr std::uint32_t kBlock = 960;

constexpr std::uint32_t frames(double milliseconds)
{
    return static_cast<std::uint32_t>(kRate * milliseconds / 1000);
}

constexpr double milliseconds(double frameCount)
{
    return frameCount * 1000 / kRate;
}

constexpr std::uint32_t kTarget = frames(50);

class Resampler
{
public:
    void apply(const Compensation& compensation)
    {
        m_compensation = compensation;
        m_left = compensation.distance;
        m_carry = 0;
    }

    std::uint32_t convert(std::uint32_t count)
    {
        if (m_left <= 0 || m_compensation.delta == 0) {
            return count;
        }
        const double exact =
            static_cast<double>(m_compensation.delta) * count / m_compensation.distance + m_carry;
        const double whole = std::floor(exact);
        m_carry = exact - whole;
        m_left -= static_cast<int>(count);
        return static_cast<std::uint32_t>(static_cast<double>(count) + whole);
    }

private:
    Compensation m_compensation;
    int m_left = 0;
    double m_carry = 0;
};

struct Link {
    double blockMs = 20.0;
    double jitterMs = 0.0;
    double periodMs = 10.0;
    double stallAtMs = -1.0;
    double stallMs = 0.0;
};

struct Outcome {
    std::uint64_t underflowAfterWarmup = 0;
    std::uint64_t dropped = 0;
    double meanLevelMs = 0;
    double lowestLevelMs = 1e9;
};

Outcome simulate(const Link& link, double seconds, double warmupSeconds = 30)
{
    AudioRegulator regulator(kChannels, kRate, kTarget);
    Resampler resampler;
    std::mt19937 random(7);
    std::uniform_real_distribution<double> lateness(0.0, link.jitterMs);
    std::vector<float> block;
    std::vector<float> out(static_cast<std::size_t>(frames(link.periodMs)) * kChannels);

    const double end = seconds * 1000;
    const double window = end - 10'000;
    std::int64_t sent = 0;
    double arrival = lateness(random);
    double nextPull = 0;
    std::uint64_t underflowAtWarmup = 0;
    double levelSum = 0;
    int levelCount = 0;
    Outcome outcome;
    while (std::min(arrival, nextPull) < end) {
        if (arrival <= nextPull) {
            block.assign(static_cast<std::size_t>(resampler.convert(kBlock)) * kChannels, 0.5f);
            if (const auto compensation = regulator.push(block, kBlock, sent * 20'000)) {
                resampler.apply(*compensation);
            }
            if (arrival >= window) {
                levelSum += milliseconds(regulator.buffered());
                ++levelCount;
            }
            ++sent;
            double next = sent * link.blockMs + lateness(random);
            if (next >= link.stallAtMs && next < link.stallAtMs + link.stallMs) {
                next = link.stallAtMs + link.stallMs;
            }
            arrival = std::max(arrival, next);
        } else {
            if (nextPull >= window) {
                outcome.lowestLevelMs = std::min(outcome.lowestLevelMs, milliseconds(regulator.buffered()));
            }
            regulator.pull(out);
            if (nextPull < warmupSeconds * 1000) {
                underflowAtWarmup = regulator.underflowFrames();
            }
            nextPull += link.periodMs;
        }
    }
    outcome.underflowAfterWarmup = regulator.underflowFrames() - underflowAtWarmup;
    outcome.dropped = regulator.droppedFrames();
    outcome.meanLevelMs = levelCount > 0 ? levelSum / levelCount : 0;
    return outcome;
}

bool silent(const std::vector<float>& samples)
{
    return std::ranges::all_of(samples, [](float sample) { return sample == 0.0f; });
}

}

TEST_CASE("playback starts once the target is buffered, and keeps at most 10 ms more before that")
{
    AudioRegulator regulator(kChannels, kRate, kTarget);
    std::vector<float> out(static_cast<std::size_t>(frames(10)) * kChannels, 1.0f);
    const std::vector<float> block(static_cast<std::size_t>(kBlock) * kChannels, 0.5f);

    regulator.pull(out);
    CHECK(silent(out));
    regulator.push(block, kBlock, 0);
    regulator.push(block, kBlock, 20'000);
    regulator.pull(out);
    CHECK(silent(out));
    CHECK(regulator.buffered() == 2 * kBlock);

    regulator.push(block, kBlock, 40'000);
    regulator.push(block, kBlock, 60'000);
    CHECK(regulator.buffered() == frames(60));
    CHECK(regulator.droppedFrames() == frames(20));

    regulator.pull(out);
    CHECK(out.front() == 0.5f);
    CHECK(regulator.buffered() == frames(50));
    CHECK(regulator.underflowFrames() == 0);
}

TEST_CASE("a steady link holds the target without running dry")
{
    for (const double period : {10.0, 3.0}) {
        CAPTURE(period);
        const Outcome outcome = simulate({.periodMs = period}, 60);
        CHECK(outcome.underflowAfterWarmup == 0);
        CHECK(outcome.dropped == 0);
        CHECK(std::abs(outcome.meanLevelMs - 50) < 5);
        CHECK(outcome.lowestLevelMs > 20);
    }
}

TEST_CASE("clock drift between phone and computer is absorbed by resampling")
{
    for (const double drift : {0.001, -0.001, 0.007, -0.007}) {
        CAPTURE(drift);
        const Outcome outcome = simulate({.blockMs = 20.0 * (1 - drift), .jitterMs = 10, .periodMs = 3}, 600);
        CHECK(outcome.underflowAfterWarmup == 0);
        CHECK(outcome.dropped == 0);
        CHECK(std::abs(outcome.meanLevelMs - 50) < 5);
    }
}

TEST_CASE("network jitter up to 15 ms plays without gaps")
{
    const Outcome outcome = simulate({.jitterMs = 15, .periodMs = 3}, 120);
    CHECK(outcome.underflowAfterWarmup == 0);
    CHECK(outcome.lowestLevelMs > 0);
}

TEST_CASE("a stall plays silence, keeps the late sound and settles back to the target")
{
    const Outcome outcome = simulate({.periodMs = 10, .stallAtMs = 40'000, .stallMs = 80}, 120);
    CHECK(outcome.underflowAfterWarmup > 0);
    CHECK(outcome.dropped == 0);
    CHECK(std::abs(outcome.meanLevelMs - 50) < 5);
}

TEST_CASE("an excess beyond the limit is cut from the start of the new block")
{
    AudioRegulator regulator(kChannels, kRate, kTarget);
    const std::vector<float> block(static_cast<std::size_t>(kBlock) * kChannels, 0.5f);
    std::vector<float> out(static_cast<std::size_t>(frames(10)) * kChannels);
    std::int64_t pts = 0;
    for (int i = 0; i < 3; ++i, pts += 20'000) {
        regulator.push(block, kBlock, pts);
    }
    regulator.pull(out);
    for (int i = 0; i < 3; ++i, pts += 20'000) {
        regulator.push(block, kBlock, pts);
    }
    CHECK(regulator.buffered() == kTarget + 3 * kBlock);

    const std::uint32_t limit = kTarget * 11 / 10 + frames(60);
    std::vector<float> marked = block;
    std::fill(marked.end() - 240 * kChannels, marked.end(), 0.9f);
    regulator.push(marked, kBlock, pts);
    CHECK(regulator.buffered() == limit);
    CHECK(regulator.droppedFrames() == kBlock - 240);

    std::vector<float> rest(static_cast<std::size_t>(limit) * kChannels);
    regulator.pull(rest);
    CHECK(rest[static_cast<std::size_t>(limit - 241) * kChannels] == 0.5f);
    CHECK(rest[static_cast<std::size_t>(limit - 240) * kChannels] == 0.9f);
    CHECK(rest.back() == 0.9f);
}

TEST_CASE("a gap in the timestamps refills to the target with silence and resets the compensation")
{
    AudioRegulator regulator(kChannels, kRate, kTarget);
    const std::vector<float> block(static_cast<std::size_t>(kBlock) * kChannels, 0.5f);
    for (int i = 0; i < 3; ++i) {
        regulator.push(block, kBlock, i * 20'000);
    }
    std::vector<float> out(static_cast<std::size_t>(frames(60)) * kChannels);
    regulator.pull(out);
    CHECK(regulator.buffered() == 0);

    const auto compensation = regulator.push(block, kBlock, 2'060'000);
    REQUIRE(compensation);
    CHECK(*compensation == Compensation{});
    CHECK(regulator.buffered() == kTarget);
}
