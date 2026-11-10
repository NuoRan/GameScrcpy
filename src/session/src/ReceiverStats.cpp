#include "ReceiverStats.h"

#include <algorithm>
#include <limits>

namespace gs::session::detail {

namespace {

template <typename T>
T saturated(std::int64_t value) noexcept
{
    return static_cast<T>(std::clamp<std::int64_t>(value, 0, std::numeric_limits<T>::max()));
}

template <typename T>
T saturated(std::uint64_t value) noexcept
{
    return static_cast<T>(std::min<std::uint64_t>(value, std::numeric_limits<T>::max()));
}

}

std::optional<protocol::ReceiverReport> ReceiverStats::add(const protocol::MediaHeader& header, std::size_t bytes,
                                                           std::int64_t arrivalUs)
{
    const std::lock_guard lock(m_mutex);
    std::optional<protocol::ReceiverReport> closed;
    if (header.kind == protocol::MediaKind::Session && header.session != m_session) {
        if (m_session != 0) {
            closed = report(arrivalUs);
        }
        m_session = header.session;
        restart(arrivalUs);
    }
    if (m_session == 0) {
        return closed;
    }
    m_bytes += bytes;
    if (header.kind == protocol::MediaKind::Frame) {
        const std::int64_t delayUs = arrivalUs - (header.ptsUs + static_cast<std::int64_t>(header.encodeDelayUs));
        m_delayMinUs = m_frames == 0 ? delayUs : std::min(m_delayMinUs, delayUs);
        m_delayMaxUs = m_frames == 0 ? delayUs : std::max(m_delayMaxUs, delayUs);
        m_delaySumUs += delayUs;
        ++m_frames;
    }
    return closed;
}

void ReceiverStats::addLoss(std::uint32_t lostShards, std::uint32_t rebuiltShards)
{
    const std::lock_guard lock(m_mutex);
    m_lostShards += lostShards;
    m_rebuiltShards += rebuiltShards;
}

std::optional<protocol::ReceiverReport> ReceiverStats::take(std::int64_t nowUs)
{
    const std::lock_guard lock(m_mutex);
    if (m_session == 0) {
        return std::nullopt;
    }
    const protocol::ReceiverReport result = report(nowUs);
    restart(nowUs);
    return result;
}

void ReceiverStats::setRoundTrip(std::int64_t roundTripUs) noexcept
{
    m_roundTripUs.store(saturated<std::uint32_t>(roundTripUs), std::memory_order_relaxed);
}

void ReceiverStats::reset()
{
    const std::lock_guard lock(m_mutex);
    m_session = 0;
    restart(0);
}

protocol::ReceiverReport ReceiverStats::report(std::int64_t nowUs) const
{
    protocol::ReceiverReport result;
    result.videoSession = m_session;
    result.intervalUs = saturated<std::uint32_t>(nowUs - m_sinceUs);
    result.frames = saturated<std::uint16_t>(m_frames);
    result.bytes = saturated<std::uint32_t>(m_bytes);
    if (m_frames > 0) {
        result.delayMinUs = m_delayMinUs;
        result.delayMeanUs = m_delaySumUs / static_cast<std::int64_t>(m_frames);
        result.delayMaxUs = m_delayMaxUs;
    }
    result.roundTripUs = m_roundTripUs.load(std::memory_order_relaxed);
    result.lostPackets = saturated<std::uint32_t>(m_lostShards);
    result.recoveredPackets = saturated<std::uint32_t>(m_rebuiltShards);
    return result;
}

void ReceiverStats::restart(std::int64_t nowUs) noexcept
{
    m_sinceUs = nowUs;
    m_bytes = 0;
    m_frames = 0;
    m_delayMinUs = 0;
    m_delayMaxUs = 0;
    m_delaySumUs = 0;
    m_lostShards = 0;
    m_rebuiltShards = 0;
}

}
