#pragma once

#include "gs/protocol/Codec.h"

#include <cstdint>
#include <optional>

namespace gs::session::detail {

class KeyFrameGate
{
public:
    struct Decision {
        bool deliver = true;
        bool askKeyFrame = false;
    };

    explicit KeyFrameGate(std::int64_t spacingUs) noexcept : m_spacingUs(spacingUs) {}

    [[nodiscard]] Decision admit(const protocol::MediaHeader& header, std::uint32_t lostBefore, std::int64_t nowUs) noexcept
    {
        Decision decision;
        if (lostBefore != 0) {
            m_waiting = true;
            if (!m_askedUs || nowUs - *m_askedUs >= m_spacingUs) {
                decision.askKeyFrame = true;
                m_askedUs = nowUs;
            }
        }
        if (m_waiting && header.kind == protocol::MediaKind::Frame) {
            if (!header.keyFrame) {
                decision.deliver = false;
                return decision;
            }
            m_waiting = false;
        }
        return decision;
    }

    [[nodiscard]] bool waiting() const noexcept { return m_waiting; }

private:
    std::int64_t m_spacingUs;
    bool m_waiting = false;
    std::optional<std::int64_t> m_askedUs;
};

}
