#include "gs/media/AudioRegulator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gs::media {

namespace {

constexpr std::int64_t kGapUs = 100'000;
constexpr int kSpreadSeconds = 4;
constexpr double kMaxRate = 0.02;
constexpr double kIntegralSeconds = 16;

}

void AudioRegulator::Average::push(float sample) noexcept
{
    if (count < kRange) {
        ++count;
    }
    value = (static_cast<float>(count - 1) * value + sample) / static_cast<float>(count);
}

AudioRegulator::AudioRegulator(int channels, int sampleRate, std::uint32_t targetFrames)
    : m_channels(channels),
      m_sampleRate(sampleRate),
      m_target(targetFrames),
      m_capacity(targetFrames + static_cast<std::uint32_t>(sampleRate)),
      m_ring(static_cast<std::size_t>(m_capacity) * static_cast<std::size_t>(channels))
{
}

std::uint32_t AudioRegulator::frames(std::uint32_t milliseconds) const noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(m_sampleRate) * milliseconds / 1000);
}

Compensation AudioRegulator::compensation(double rate) const noexcept
{
    const int distance = kSpreadSeconds * m_sampleRate;
    return Compensation{static_cast<int>(std::lround(rate * distance)), distance};
}

std::uint32_t AudioRegulator::buffered() const noexcept
{
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    return static_cast<std::uint32_t>(head - tail);
}

std::uint32_t AudioRegulator::write(const float* data, std::uint32_t count) noexcept
{
    const std::uint64_t head = m_head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
    count = std::min(count, m_capacity - static_cast<std::uint32_t>(head - tail));
    const auto channels = static_cast<std::size_t>(m_channels);
    const auto start = static_cast<std::uint32_t>(head % m_capacity);
    const std::uint32_t first = std::min(count, m_capacity - start);
    float* const ring = m_ring.data();
    if (data == nullptr) {
        std::fill_n(ring + start * channels, first * channels, 0.0f);
        std::fill_n(ring, (count - first) * channels, 0.0f);
    } else {
        std::memcpy(ring + start * channels, data, first * channels * sizeof(float));
        std::memcpy(ring, data + first * channels, (count - first) * channels * sizeof(float));
    }
    m_head.store(head + count, std::memory_order_release);
    return count;
}

std::uint32_t AudioRegulator::read(float* data, std::uint32_t count) noexcept
{
    const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    count = std::min(count, static_cast<std::uint32_t>(head - tail));
    const auto channels = static_cast<std::size_t>(m_channels);
    const auto start = static_cast<std::uint32_t>(tail % m_capacity);
    const std::uint32_t first = std::min(count, m_capacity - start);
    const float* const ring = m_ring.data();
    std::memcpy(data, ring + start * channels, first * channels * sizeof(float));
    std::memcpy(data + first * channels, ring, (count - first) * channels * sizeof(float));
    m_tail.store(tail + count, std::memory_order_release);
    return count;
}

std::optional<Compensation> AudioRegulator::push(std::span<const float> samples, std::uint32_t inputFrames,
                                                 std::int64_t ptsUs)
{
    std::optional<Compensation> result;
    const auto count = static_cast<std::uint32_t>(samples.size() / static_cast<std::size_t>(m_channels));

    if (m_nextPtsUs != 0 && ptsUs - m_nextPtsUs > kGapUs) {
        const std::uint32_t level = buffered();
        if (level + count < m_target) {
            write(nullptr, m_target - level - count);
        }
        m_average.value = static_cast<float>(m_target);
        m_sinceResync = 0;
        m_compensating = false;
        m_underflow.store(0, std::memory_order_relaxed);
        result = m_drift != 0 ? compensation(m_drift) : Compensation{};
    }
    m_nextPtsUs = ptsUs + static_cast<std::int64_t>(inputFrames) * 1'000'000 / m_sampleRate;

    const bool played = m_played.load(std::memory_order_acquire);
    const std::uint32_t limit = played ? m_target * 11 / 10 + frames(60) : m_target + frames(10);
    const std::uint32_t level = buffered();
    const std::uint32_t excess = level + count > limit ? std::min(count, level + count - limit) : 0;
    const std::uint32_t written =
        write(samples.data() + static_cast<std::size_t>(excess) * static_cast<std::size_t>(m_channels), count - excess);
    const std::uint32_t dropped = count - written;
    if (dropped > 0) {
        m_droppedTotal.fetch_add(dropped, std::memory_order_relaxed);
    }
    m_received.store(true, std::memory_order_release);
    if (!played) {
        return result;
    }

    const std::uint32_t underflow = m_underflow.exchange(0, std::memory_order_relaxed);
    m_underflowTotal.fetch_add(underflow, std::memory_order_relaxed);
    const auto instant = static_cast<float>(static_cast<std::int64_t>(underflow) - static_cast<std::int64_t>(dropped));
    m_average.value = std::max(0.0f, m_average.value + instant);
    m_average.push(static_cast<float>(buffered()));

    m_sinceResync += written;
    if (m_sinceResync >= static_cast<std::uint32_t>(m_sampleRate)) {
        m_sinceResync = 0;
        const double distance = static_cast<double>(kSpreadSeconds) * m_sampleRate;
        const double error = static_cast<double>(m_target) - static_cast<double>(m_average.value);
        m_drift = std::clamp(m_drift + error / distance / kIntegralSeconds, -kMaxRate, kMaxRate);
        const double threshold = (m_compensating ? 1.0 : 4.0) * m_sampleRate / 1000;
        const double proportional = std::abs(error) < threshold ? 0.0 : error / distance;
        m_compensating = proportional != 0.0;
        double rate = std::clamp(m_drift + proportional, -kMaxRate, kMaxRate);
        if (rate < 0 && buffered() < m_target) {
            rate = 0;
        }
        result = compensation(rate);
    }
    return result;
}

void AudioRegulator::pull(std::span<float> out) noexcept
{
    const auto wanted = static_cast<std::uint32_t>(out.size() / static_cast<std::size_t>(m_channels));
    if (!m_played.load(std::memory_order_relaxed) && buffered() < m_target) {
        std::ranges::fill(out, 0.0f);
        return;
    }
    const std::uint32_t got = read(out.data(), wanted);
    if (got < wanted) {
        std::fill(out.begin() + static_cast<std::ptrdiff_t>(got) * m_channels, out.end(), 0.0f);
        if (m_received.load(std::memory_order_acquire)) {
            m_underflow.fetch_add(wanted - got, std::memory_order_relaxed);
        }
    }
    m_played.store(true, std::memory_order_release);
}

}
