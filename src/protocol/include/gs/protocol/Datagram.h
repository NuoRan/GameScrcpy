#pragma once

#include <gs/base/Expected.h>
#include <gs/protocol/Errors.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gs::protocol {

inline constexpr std::size_t kDatagramHeaderSize = 8;
inline constexpr std::size_t kDatagramMaxSize = 1280;
inline constexpr std::uint16_t kDatagramMagic = 0x4753;
inline constexpr std::uint8_t kDatagramHandshakeChannel = 15;

inline constexpr std::string_view kUdpHelloContext = "GSCP/1 udp hello";
inline constexpr std::string_view kUdpControlContext = "GSCP/1 udp control";
inline constexpr std::size_t kControlTagSize = 16;

struct DatagramHeader {
    std::uint8_t channel = 0;
    std::uint32_t connection = 0;
};

[[nodiscard]] constexpr bool isDatagramChannel(std::uint8_t channel) noexcept
{
    return channel <= 3 || channel == kDatagramHandshakeChannel;
}

void encodeDatagramHeader(const DatagramHeader& header, std::span<std::uint8_t, kDatagramHeaderSize> out) noexcept;

[[nodiscard]] std::array<std::uint8_t, kDatagramHeaderSize> encodeDatagramHeader(const DatagramHeader& header) noexcept;

[[nodiscard]] Expected<DatagramHeader, DecodeError> decodeDatagramHeader(std::span<const std::uint8_t> datagram) noexcept;

}
