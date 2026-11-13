#pragma once

#include "gs/protocol/Codec.h"
#include "gs/session/Session.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace gs::session::detail {

class KeyFrameGate
{
public:
    struct Decision {
        bool deliver = true;
        bool askKeyFrame = false;
        std::optional<VideoStall> stall;
    };

    static constexpr std::int64_t kShortestRetryUs = 100'000;
    static constexpr std::int64_t kLongestRetryUs = 500'000;
    static constexpr std::int64_t kAssumedRoundTripUs = 100'000;
    static constexpr std::int64_t kLongestFrameGapUs = 100'000;
    static constexpr std::int64_t kAssumedFrameGapUs = 1'000'000 / 60;

    [[nodiscard]] Decision admit(const protocol::MediaHeader& header, std::uint32_t lostBefore, std::int64_t nowUs,
                                 std::int64_t roundTripUs) noexcept
    {
        const bool frame = header.kind == protocol::MediaKind::Frame;
        const bool keyFrame = frame && header.keyFrame;
        if (frame) {
            if (m_lastPtsUs) {
                m_frameGapUs = header.ptsUs - *m_lastPtsUs;
            }
            m_lastPtsUs = header.ptsUs;
        }
        Decision decision;
        if (!m_waiting) {
            if (lostBefore == 0 || keyFrame) {
                return decision;
            }
            m_waiting = true;
            m_sinceUs = nowUs;
            m_lost = 0;
            m_requests = 0;
            m_retryUs = firstRetryUs(roundTripUs);
            ask(decision, nowUs);
        } else if (!keyFrame && nowUs - m_askedUs >= m_retryUs) {
            ask(decision, nowUs);
            m_retryUs = std::min(2 * m_retryUs, kLongestRetryUs);
        }
        m_lost = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(std::uint64_t{m_lost} + lostBefore, std::numeric_limits<std::uint32_t>::max()));
        if (keyFrame) {
            m_waiting = false;
            decision.stall = VideoStall{.waited = std::chrono::microseconds(nowUs - m_sinceUs),
                                        .lostPackets = m_lost,
                                        .keyFrameRequests = m_requests};
            return decision;
        }
        decision.deliver = !frame;
        return decision;
    }

    [[nodiscard]] bool waiting() const noexcept { return m_waiting; }

private:
    [[nodiscard]] std::int64_t firstRetryUs(std::int64_t roundTripUs) const noexcept
    {
        const std::int64_t roundTrip = roundTripUs > 0 ? std::min(roundTripUs, kLongestRetryUs) : kAssumedRoundTripUs;
        const std::int64_t frameGap = m_frameGapUs && *m_frameGapUs >= 0 && *m_frameGapUs <= kLongestFrameGapUs
                                          ? *m_frameGapUs
                                          : kAssumedFrameGapUs;
        return std::clamp(3 * roundTrip + 2 * frameGap, kShortestRetryUs, kLongestRetryUs);
    }

    void ask(Decision& decision, std::int64_t nowUs) noexcept
    {
        decision.askKeyFrame = true;
        m_askedUs = nowUs;
        ++m_requests;
    }

    bool m_waiting = false;
    std::optional<std::int64_t> m_lastPtsUs;
    std::optional<std::int64_t> m_frameGapUs;
    std::int64_t m_sinceUs = 0;
    std::int64_t m_askedUs = 0;
    std::int64_t m_retryUs = kShortestRetryUs;
    std::uint32_t m_lost = 0;
    std::uint32_t m_requests = 0;
};

}
