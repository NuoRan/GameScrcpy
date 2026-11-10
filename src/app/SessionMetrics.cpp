#include "SessionMetrics.h"

#include <gs/session/Session.h>

#include <algorithm>

namespace gs::app {

namespace {

double milliseconds(std::int64_t microseconds)
{
    return static_cast<double>(microseconds) / 1000.0;
}

}

template <std::size_t N>
std::optional<double> SessionMetrics::Window<N>::mean() const noexcept
{
    if (count == 0) {
        return std::nullopt;
    }
    std::int64_t sum = 0;
    for (std::size_t age = 0; age < count; ++age) {
        sum += at(age);
    }
    return milliseconds(sum) / static_cast<double>(count);
}

void SessionMetrics::videoFrame(std::uint32_t bytes, std::int64_t ptsUs, std::uint32_t encodeDelayUs,
                                std::int64_t receivedUs, std::int64_t decodeUs) noexcept
{
    m_frames.fetch_add(1, std::memory_order_relaxed);
    m_bytes.fetch_add(bytes, std::memory_order_relaxed);
    m_decodeUs.fetch_add(decodeUs, std::memory_order_relaxed);
    m_encodeUs.fetch_add(encodeDelayUs, std::memory_order_relaxed);
    if (m_clockKnown.load(std::memory_order_acquire)) {
        const std::int64_t capturedUs = ptsUs - m_clockOffsetUs.load(std::memory_order_relaxed);
        const std::int64_t latencyUs = receivedUs - capturedUs;
        m_latencyUs.fetch_add(latencyUs, std::memory_order_relaxed);
        m_networkUs.fetch_add(latencyUs - encodeDelayUs, std::memory_order_relaxed);
        m_latencyFrames.fetch_add(1, std::memory_order_relaxed);
    }

    const std::lock_guard lock(m_timingMutex);
    if (m_capturedPts.count > 0) {
        const std::int64_t previous = m_capturedPts.at(0);
        const bool live = ptsUs - previous <= kLiveFrameGapUs;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < m_waitingCount; ++i) {
            const std::int64_t injectedUs = m_waiting[i];
            if (injectedUs > ptsUs) {
                m_waiting[kept++] = injectedUs;
            } else if (live && injectedUs >= previous) {
                m_frameWait.add(ptsUs - injectedUs);
            }
        }
        m_waitingCount = kept;
    }
    m_capturedPts.add(ptsUs);
}

void SessionMetrics::heartbeat(const session::Heartbeat& heartbeat) noexcept
{
    m_roundTripUs.store(heartbeat.roundTrip.count(), std::memory_order_relaxed);
    m_sendDelayUs.store(heartbeat.sendDelay.count(), std::memory_order_relaxed);
    m_clockOffsetUs.store(heartbeat.clockOffset.count(), std::memory_order_relaxed);
    m_clockKnown.store(true, std::memory_order_release);
    if (heartbeat.sentUs <= 0 || heartbeat.serverTimeUs <= 0) {
        return;
    }
    const std::lock_guard lock(m_timingMutex);
    m_input.add(heartbeat.serverTimeUs - heartbeat.clockOffset.count() - heartbeat.sentUs);
    waitForFrameAfter(heartbeat.serverTimeUs);
}

void SessionMetrics::waitForFrameAfter(std::int64_t injectedUs) noexcept
{
    const std::size_t count = m_capturedPts.count;
    if (count == 0 || m_capturedPts.at(0) < injectedUs) {
        if (m_waitingCount == m_waiting.size()) {
            std::shift_left(m_waiting.begin(), m_waiting.end(), 1);
            --m_waitingCount;
        }
        m_waiting[m_waitingCount++] = injectedUs;
        return;
    }
    for (std::size_t age = count - 1; age > 0; --age) {
        const std::int64_t before = m_capturedPts.at(age);
        const std::int64_t after = m_capturedPts.at(age - 1);
        if (before <= injectedUs && after >= injectedUs) {
            if (after - before <= kLiveFrameGapUs) {
                m_frameWait.add(after - injectedUs);
            }
            return;
        }
    }
}

std::optional<double> SessionMetrics::halfFrameInterval() const noexcept
{
    std::int64_t sum = 0;
    std::size_t intervals = 0;
    for (std::size_t age = 0; age + 1 < m_capturedPts.count; ++age) {
        const std::int64_t gap = m_capturedPts.at(age) - m_capturedPts.at(age + 1);
        if (gap > 0 && gap <= kLiveFrameGapUs) {
            sum += gap;
            ++intervals;
        }
    }
    if (intervals == 0) {
        return std::nullopt;
    }
    return milliseconds(sum) / static_cast<double>(intervals) / 2.0;
}

SessionMetrics::Sample SessionMetrics::sample(std::chrono::microseconds elapsed) noexcept
{
    Sample result;
    const std::uint64_t frames = m_frames.exchange(0, std::memory_order_relaxed);
    const std::uint64_t bytes = m_bytes.exchange(0, std::memory_order_relaxed);
    const std::int64_t decodeUs = m_decodeUs.exchange(0, std::memory_order_relaxed);
    const std::int64_t encodeUs = m_encodeUs.exchange(0, std::memory_order_relaxed);
    const std::int64_t latencyUs = m_latencyUs.exchange(0, std::memory_order_relaxed);
    const std::int64_t networkUs = m_networkUs.exchange(0, std::memory_order_relaxed);
    const std::uint64_t latencyFrames = m_latencyFrames.exchange(0, std::memory_order_relaxed);
    const double seconds = std::chrono::duration<double>(elapsed).count();
    if (seconds > 0) {
        result.framesPerSecond = static_cast<double>(frames) / seconds;
        result.megabitsPerSecond = static_cast<double>(bytes) * 8 / 1e6 / seconds;
    }
    if (frames > 0) {
        result.decodeMs = milliseconds(decodeUs) / static_cast<double>(frames);
        result.encodeMs = milliseconds(encodeUs) / static_cast<double>(frames);
    }
    if (latencyFrames > 0) {
        result.captureToReceiptMs = milliseconds(latencyUs) / static_cast<double>(latencyFrames);
        result.networkMs = milliseconds(networkUs) / static_cast<double>(latencyFrames);
    }
    if (m_clockKnown.load(std::memory_order_acquire)) {
        result.roundTripMs = milliseconds(m_roundTripUs.load(std::memory_order_relaxed));
        result.sendDelayMs = milliseconds(m_sendDelayUs.load(std::memory_order_relaxed));
    }
    const std::lock_guard lock(m_timingMutex);
    if (const std::optional<double> input = m_input.mean()) {
        result.inputMs = std::max(0.0, *input);
    }
    result.frameWaitMs = m_frameWait.count > 0 ? m_frameWait.mean() : halfFrameInterval();
    return result;
}

}
