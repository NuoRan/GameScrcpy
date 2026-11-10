#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace gs::espflash {

inline constexpr std::uint8_t kSlipEnd = 0xC0;
inline constexpr std::uint8_t kSlipEscape = 0xDB;
inline constexpr std::uint8_t kSlipEscapedEnd = 0xDC;
inline constexpr std::uint8_t kSlipEscapedEscape = 0xDD;
inline constexpr std::size_t kMostSlipPacket = 64 * 1024;

[[nodiscard]] std::vector<std::uint8_t> slipEncode(std::span<const std::uint8_t> packet);

class SlipDecoder
{
public:
    void feed(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> next();
    void clear() noexcept;

private:
    std::vector<std::uint8_t> m_current;
    std::deque<std::vector<std::uint8_t>> m_packets;
    bool m_inside = false;
    bool m_escaped = false;
    bool m_overflow = false;
};

}
