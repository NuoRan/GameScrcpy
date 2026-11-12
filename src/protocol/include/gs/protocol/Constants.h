#pragma once

#include <cstddef>
#include <cstdint>

namespace gs::protocol {

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint32_t kMagic = 0x47534350;

inline constexpr std::size_t kFrameHeaderSize = 6;
inline constexpr std::size_t kMediaHeaderSize = 20;

inline constexpr std::uint32_t kHandshakePayloadLimit = 16U * 1024U;
inline constexpr std::uint32_t kControlPayloadLimit = 256U * 1024U;
inline constexpr std::uint32_t kBulkPayloadLimit = 64U * 1024U * 1024U;
inline constexpr std::uint32_t kCompanionPayloadLimit = 16U * 1024U;
inline constexpr std::uint32_t kVideoPayloadLimit = 16U * 1024U * 1024U;
inline constexpr std::uint32_t kAudioPayloadLimit = 64U * 1024U;

inline constexpr std::uint8_t kMaxPointerId = 31;

inline constexpr std::uint16_t kCompanionVersion = 2;
inline constexpr std::uint16_t kCompanionSettingsVersion = 2;
inline constexpr std::size_t kCompanionTokenSize = 16;
inline constexpr std::size_t kCompanionTextLimit = 4096;
inline constexpr std::size_t kMaxKeyHints = 192;
inline constexpr std::size_t kMaxKeyHintLabel = 32;
inline constexpr std::size_t kMaxScreenshotChunk = 15U * 1024U;
inline constexpr std::uint16_t kMaxScreenshotChunks = 274;
inline constexpr std::size_t kMaxScreenshotBytes = 4U * 1024U * 1024U;
inline constexpr std::uint16_t kDefaultScreenshotSide = 1920;
inline constexpr std::uint8_t kDefaultScreenshotQuality = 90;
inline constexpr std::size_t kCompanionComputerIdSize = 16;
inline constexpr std::size_t kMaxComputerName = 64;
inline constexpr std::uint32_t kAccentMask = 0x00FFFFFFU;
inline constexpr std::uint8_t kMinPhoneHintOpacity = 10;
inline constexpr std::uint8_t kMaxPhoneHintOpacity = 100;
inline constexpr std::uint8_t kDefaultPhoneHintOpacity = 60;

enum class FrameContext : std::uint8_t {
    Handshake,
    Control,
    Bulk,
    Companion,
};

enum class Direction : std::uint8_t {
    ToServer,
    ToClient,
};

enum class MediaChannel : std::uint8_t {
    Video,
    Audio,
};

[[nodiscard]] constexpr std::uint32_t payloadLimit(FrameContext context) noexcept
{
    switch (context) {
    case FrameContext::Handshake:
        return kHandshakePayloadLimit;
    case FrameContext::Control:
        return kControlPayloadLimit;
    case FrameContext::Bulk:
        return kBulkPayloadLimit;
    case FrameContext::Companion:
        return kCompanionPayloadLimit;
    }
    return 0;
}

[[nodiscard]] constexpr std::uint32_t payloadLimit(MediaChannel channel) noexcept
{
    return channel == MediaChannel::Video ? kVideoPayloadLimit : kAudioPayloadLimit;
}

}
