#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace gs::protocol {

template <typename E>
struct EnumEntry {
    E value;
    std::string_view name;
};

template <typename E>
struct EnumTraits;

template <typename E>
[[nodiscard]] constexpr bool isValidEnumValue(std::underlying_type_t<E> raw) noexcept
{
    for (const auto& entry : EnumTraits<E>::entries) {
        if (static_cast<std::underlying_type_t<E>>(entry.value) == raw) {
            return true;
        }
    }
    return false;
}

template <typename E>
[[nodiscard]] constexpr std::string_view enumName(E value) noexcept
{
    for (const auto& entry : EnumTraits<E>::entries) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return {};
}

template <typename E>
[[nodiscard]] constexpr std::optional<E> enumFromName(std::string_view name) noexcept
{
    for (const auto& entry : EnumTraits<E>::entries) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

enum class TouchAction : std::uint8_t { Down = 0, Up = 1, Move = 2, Cancel = 3 };
enum class KeyAction : std::uint8_t { Down = 0, Up = 1 };
enum class VideoSource : std::uint8_t { Display = 0, VirtualDisplay = 1 };
enum class VideoCodec : std::uint8_t { H264 = 1, H265 = 2, Av1 = 3 };
enum class AudioCodec : std::uint8_t { Opus = 1 };
enum class CopyKey : std::uint8_t { None = 0, Copy = 1, Cut = 2 };
enum class OrientationMode : std::uint8_t { Follow = 0, Lock0 = 1, Lock90 = 2, Lock180 = 3, Lock270 = 4 };
enum class SystemPanelAction : std::uint8_t { ExpandNotifications = 0, ExpandQuickSettings = 1, Collapse = 2 };
enum class NoticeLevel : std::uint8_t { Info = 0, Warning = 1, Error = 2 };
enum class AudioStatus : std::uint8_t { Available = 0, UnsupportedAndroid = 1, CaptureFailed = 2 };
enum class ChannelId : std::uint8_t { Video = 1, Audio = 2, Bulk = 3 };
enum class ScreenshotFormat : std::uint8_t { Png = 0 };
enum class MediaKind : std::uint8_t { Session = 0, Config = 1, Frame = 2 };
enum class OverlayStatus : std::uint8_t { Granted = 0, NoPermission = 1 };
enum class ImeAction : std::uint8_t {
    Unspecified = 0,
    None = 1,
    Go = 2,
    Search = 3,
    Send = 4,
    Next = 5,
    Done = 6,
    Previous = 7,
};
enum class KeyHintShape : std::uint8_t {
    Tap = 0,
    Hold = 1,
    LongPress = 2,
    Turbo = 3,
    Camera = 4,
    Aim = 5,
    FreeLook = 6,
    Swipe = 7,
    Key = 8,
    Ring = 9,
    Unbound = 10,
};
enum class ScreenshotProblem : std::uint8_t { Refused = 0, Unsupported = 1, Busy = 2, Error = 3 };

enum class ErrorCode : std::uint16_t {
    None = 0,
    VersionMismatch = 1,
    AuthenticationFailed = 2,
    UnsupportedAndroid = 3,
    MalformedFrame = 4,
    FrameTooLarge = 5,
    UnknownType = 6,
    WrongDirection = 7,
    UnexpectedMessage = 8,
    ChannelUnavailable = 9,
    ResourceBusy = 10,
    EncoderFailure = 11,
    PermissionDenied = 12,
    NotFound = 13,
    Timeout = 14,
    InternalError = 15,
    InjectionDenied = 16,
    ClipboardDenied = 17,
    AudioCaptureFailed = 18,
    VideoReduced = 19,
    VideoRestored = 20,
};

template <>
struct EnumTraits<TouchAction> {
    static constexpr std::array<EnumEntry<TouchAction>, 4> entries{{
        {TouchAction::Down, "down"},
        {TouchAction::Up, "up"},
        {TouchAction::Move, "move"},
        {TouchAction::Cancel, "cancel"},
    }};
};

template <>
struct EnumTraits<KeyAction> {
    static constexpr std::array<EnumEntry<KeyAction>, 2> entries{{
        {KeyAction::Down, "down"},
        {KeyAction::Up, "up"},
    }};
};

template <>
struct EnumTraits<VideoSource> {
    static constexpr std::array<EnumEntry<VideoSource>, 2> entries{{
        {VideoSource::Display, "display"},
        {VideoSource::VirtualDisplay, "virtualDisplay"},
    }};
};

template <>
struct EnumTraits<VideoCodec> {
    static constexpr std::array<EnumEntry<VideoCodec>, 3> entries{{
        {VideoCodec::H264, "h264"},
        {VideoCodec::H265, "h265"},
        {VideoCodec::Av1, "av1"},
    }};
};

template <>
struct EnumTraits<AudioCodec> {
    static constexpr std::array<EnumEntry<AudioCodec>, 1> entries{{
        {AudioCodec::Opus, "opus"},
    }};
};

template <>
struct EnumTraits<CopyKey> {
    static constexpr std::array<EnumEntry<CopyKey>, 3> entries{{
        {CopyKey::None, "none"},
        {CopyKey::Copy, "copy"},
        {CopyKey::Cut, "cut"},
    }};
};

template <>
struct EnumTraits<OrientationMode> {
    static constexpr std::array<EnumEntry<OrientationMode>, 5> entries{{
        {OrientationMode::Follow, "follow"},
        {OrientationMode::Lock0, "lock0"},
        {OrientationMode::Lock90, "lock90"},
        {OrientationMode::Lock180, "lock180"},
        {OrientationMode::Lock270, "lock270"},
    }};
};

template <>
struct EnumTraits<SystemPanelAction> {
    static constexpr std::array<EnumEntry<SystemPanelAction>, 3> entries{{
        {SystemPanelAction::ExpandNotifications, "expandNotifications"},
        {SystemPanelAction::ExpandQuickSettings, "expandQuickSettings"},
        {SystemPanelAction::Collapse, "collapse"},
    }};
};

template <>
struct EnumTraits<NoticeLevel> {
    static constexpr std::array<EnumEntry<NoticeLevel>, 3> entries{{
        {NoticeLevel::Info, "info"},
        {NoticeLevel::Warning, "warning"},
        {NoticeLevel::Error, "error"},
    }};
};

template <>
struct EnumTraits<AudioStatus> {
    static constexpr std::array<EnumEntry<AudioStatus>, 3> entries{{
        {AudioStatus::Available, "available"},
        {AudioStatus::UnsupportedAndroid, "unsupportedAndroid"},
        {AudioStatus::CaptureFailed, "captureFailed"},
    }};
};

template <>
struct EnumTraits<ChannelId> {
    static constexpr std::array<EnumEntry<ChannelId>, 3> entries{{
        {ChannelId::Video, "video"},
        {ChannelId::Audio, "audio"},
        {ChannelId::Bulk, "bulk"},
    }};
};

template <>
struct EnumTraits<ScreenshotFormat> {
    static constexpr std::array<EnumEntry<ScreenshotFormat>, 1> entries{{
        {ScreenshotFormat::Png, "png"},
    }};
};

template <>
struct EnumTraits<MediaKind> {
    static constexpr std::array<EnumEntry<MediaKind>, 3> entries{{
        {MediaKind::Session, "session"},
        {MediaKind::Config, "config"},
        {MediaKind::Frame, "frame"},
    }};
};

template <>
struct EnumTraits<OverlayStatus> {
    static constexpr std::array<EnumEntry<OverlayStatus>, 2> entries{{
        {OverlayStatus::Granted, "granted"},
        {OverlayStatus::NoPermission, "noPermission"},
    }};
};

template <>
struct EnumTraits<ImeAction> {
    static constexpr std::array<EnumEntry<ImeAction>, 8> entries{{
        {ImeAction::Unspecified, "unspecified"},
        {ImeAction::None, "none"},
        {ImeAction::Go, "go"},
        {ImeAction::Search, "search"},
        {ImeAction::Send, "send"},
        {ImeAction::Next, "next"},
        {ImeAction::Done, "done"},
        {ImeAction::Previous, "previous"},
    }};
};

template <>
struct EnumTraits<KeyHintShape> {
    static constexpr std::array<EnumEntry<KeyHintShape>, 11> entries{{
        {KeyHintShape::Tap, "tap"},
        {KeyHintShape::Hold, "hold"},
        {KeyHintShape::LongPress, "longPress"},
        {KeyHintShape::Turbo, "turbo"},
        {KeyHintShape::Camera, "camera"},
        {KeyHintShape::Aim, "aim"},
        {KeyHintShape::FreeLook, "freeLook"},
        {KeyHintShape::Swipe, "swipe"},
        {KeyHintShape::Key, "key"},
        {KeyHintShape::Ring, "ring"},
        {KeyHintShape::Unbound, "unbound"},
    }};
};

template <>
struct EnumTraits<ScreenshotProblem> {
    static constexpr std::array<EnumEntry<ScreenshotProblem>, 4> entries{{
        {ScreenshotProblem::Refused, "refused"},
        {ScreenshotProblem::Unsupported, "unsupported"},
        {ScreenshotProblem::Busy, "busy"},
        {ScreenshotProblem::Error, "error"},
    }};
};

template <>
struct EnumTraits<ErrorCode> {
    static constexpr std::array<EnumEntry<ErrorCode>, 21> entries{{
        {ErrorCode::None, "none"},
        {ErrorCode::VersionMismatch, "versionMismatch"},
        {ErrorCode::AuthenticationFailed, "authenticationFailed"},
        {ErrorCode::UnsupportedAndroid, "unsupportedAndroid"},
        {ErrorCode::MalformedFrame, "malformedFrame"},
        {ErrorCode::FrameTooLarge, "frameTooLarge"},
        {ErrorCode::UnknownType, "unknownType"},
        {ErrorCode::WrongDirection, "wrongDirection"},
        {ErrorCode::UnexpectedMessage, "unexpectedMessage"},
        {ErrorCode::ChannelUnavailable, "channelUnavailable"},
        {ErrorCode::ResourceBusy, "resourceBusy"},
        {ErrorCode::EncoderFailure, "encoderFailure"},
        {ErrorCode::PermissionDenied, "permissionDenied"},
        {ErrorCode::NotFound, "notFound"},
        {ErrorCode::Timeout, "timeout"},
        {ErrorCode::InternalError, "internalError"},
        {ErrorCode::InjectionDenied, "injectionDenied"},
        {ErrorCode::ClipboardDenied, "clipboardDenied"},
        {ErrorCode::AudioCaptureFailed, "audioCaptureFailed"},
        {ErrorCode::VideoReduced, "videoReduced"},
        {ErrorCode::VideoRestored, "videoRestored"},
    }};
};

}
