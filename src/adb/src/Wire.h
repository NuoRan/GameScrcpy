#pragma once

#include <cstdint>

namespace gs::adb::detail {

[[nodiscard]] constexpr std::uint32_t loadLe32(const std::uint8_t* in) noexcept
{
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8U) |
           (static_cast<std::uint32_t>(in[2]) << 16U) | (static_cast<std::uint32_t>(in[3]) << 24U);
}

[[nodiscard]] constexpr std::uint64_t loadLe64(const std::uint8_t* in) noexcept
{
    return static_cast<std::uint64_t>(loadLe32(in)) | (static_cast<std::uint64_t>(loadLe32(in + 4)) << 32U);
}

constexpr void storeLe32(std::uint32_t value, std::uint8_t* out) noexcept
{
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8U);
    out[2] = static_cast<std::uint8_t>(value >> 16U);
    out[3] = static_cast<std::uint8_t>(value >> 24U);
}

}
