#pragma once

#include <cstdint>
#include <string_view>

namespace gs::protocol {

enum class DecodeError : std::uint8_t {
    Truncated,
    TrailingBytes,
    LengthExceeded,
    InvalidEnum,
    InvalidBool,
    MalformedUtf8,
    ReservedBitsSet,
    OutOfRange,
    BadMagic,
    UnknownType,
    WrongDirection,
    UnexpectedType,
    FrameTooLarge,
};

enum class EncodeError : std::uint8_t {
    FieldTooLong,
    ValueOutOfRange,
    MalformedUtf8,
    FrameTooLarge,
    BufferTooSmall,
};

[[nodiscard]] std::string_view errorName(DecodeError error) noexcept;
[[nodiscard]] std::string_view errorName(EncodeError error) noexcept;

}
