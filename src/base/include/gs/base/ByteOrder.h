#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gs {

template <std::unsigned_integral T>
    requires(sizeof(T) <= sizeof(std::uint64_t))
[[nodiscard]] constexpr T loadBigEndian(std::span<const std::uint8_t, sizeof(T)> in) noexcept
{
    std::uint64_t value = 0;
    for (const std::uint8_t byte : in) {
        value = (value << 8U) | byte;
    }
    return static_cast<T>(value);
}

template <std::unsigned_integral T>
    requires(sizeof(T) <= sizeof(std::uint64_t))
constexpr void storeBigEndian(T value, std::span<std::uint8_t, sizeof(T)> out) noexcept
{
    auto remaining = static_cast<std::uint64_t>(value);
    for (std::size_t i = sizeof(T); i-- > 0;) {
        out[i] = static_cast<std::uint8_t>(remaining & 0xFFU);
        remaining >>= 8U;
    }
}

}
