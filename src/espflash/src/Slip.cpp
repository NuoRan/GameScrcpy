#include "gs/espflash/Slip.h"

namespace gs::espflash {

std::vector<std::uint8_t> slipEncode(std::span<const std::uint8_t> packet)
{
    std::vector<std::uint8_t> out;
    out.reserve(packet.size() + packet.size() / 8 + 2);
    out.push_back(kSlipEnd);
    for (const std::uint8_t byte : packet) {
        if (byte == kSlipEnd) {
            out.push_back(kSlipEscape);
            out.push_back(kSlipEscapedEnd);
        } else if (byte == kSlipEscape) {
            out.push_back(kSlipEscape);
            out.push_back(kSlipEscapedEscape);
        } else {
            out.push_back(byte);
        }
    }
    out.push_back(kSlipEnd);
    return out;
}

void SlipDecoder::feed(std::span<const std::uint8_t> bytes)
{
    for (const std::uint8_t byte : bytes) {
        if (byte == kSlipEnd) {
            if (m_inside && !m_current.empty() && !m_overflow) {
                m_packets.push_back(std::move(m_current));
            }
            m_current.clear();
            m_inside = true;
            m_escaped = false;
            m_overflow = false;
            continue;
        }
        if (!m_inside) {
            continue;
        }
        std::uint8_t value = byte;
        if (m_escaped) {
            m_escaped = false;
            if (byte == kSlipEscapedEnd) {
                value = kSlipEnd;
            } else if (byte == kSlipEscapedEscape) {
                value = kSlipEscape;
            } else {
                m_overflow = true;
            }
        } else if (byte == kSlipEscape) {
            m_escaped = true;
            continue;
        }
        if (m_current.size() >= kMostSlipPacket) {
            m_overflow = true;
            continue;
        }
        m_current.push_back(value);
    }
}

std::optional<std::vector<std::uint8_t>> SlipDecoder::next()
{
    if (m_packets.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> packet = std::move(m_packets.front());
    m_packets.pop_front();
    return packet;
}

void SlipDecoder::clear() noexcept
{
    m_current.clear();
    m_packets.clear();
    m_inside = false;
    m_escaped = false;
    m_overflow = false;
}

}
