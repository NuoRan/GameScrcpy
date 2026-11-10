#include "gs/protocol/Errors.h"

namespace gs::protocol {

std::string_view errorName(DecodeError error) noexcept
{
    switch (error) {
    case DecodeError::Truncated:
        return "Truncated";
    case DecodeError::TrailingBytes:
        return "TrailingBytes";
    case DecodeError::LengthExceeded:
        return "LengthExceeded";
    case DecodeError::InvalidEnum:
        return "InvalidEnum";
    case DecodeError::InvalidBool:
        return "InvalidBool";
    case DecodeError::MalformedUtf8:
        return "MalformedUtf8";
    case DecodeError::ReservedBitsSet:
        return "ReservedBitsSet";
    case DecodeError::OutOfRange:
        return "OutOfRange";
    case DecodeError::BadMagic:
        return "BadMagic";
    case DecodeError::UnknownType:
        return "UnknownType";
    case DecodeError::WrongDirection:
        return "WrongDirection";
    case DecodeError::UnexpectedType:
        return "UnexpectedType";
    case DecodeError::FrameTooLarge:
        return "FrameTooLarge";
    }
    return "Unknown";
}

std::string_view errorName(EncodeError error) noexcept
{
    switch (error) {
    case EncodeError::FieldTooLong:
        return "FieldTooLong";
    case EncodeError::ValueOutOfRange:
        return "ValueOutOfRange";
    case EncodeError::MalformedUtf8:
        return "MalformedUtf8";
    case EncodeError::FrameTooLarge:
        return "FrameTooLarge";
    case EncodeError::BufferTooSmall:
        return "BufferTooSmall";
    }
    return "Unknown";
}

}
