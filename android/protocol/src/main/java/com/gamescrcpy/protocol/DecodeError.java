package com.gamescrcpy.protocol;

public enum DecodeError {
    TRUNCATED("Truncated"),
    TRAILING_BYTES("TrailingBytes"),
    LENGTH_EXCEEDED("LengthExceeded"),
    INVALID_ENUM("InvalidEnum"),
    INVALID_BOOL("InvalidBool"),
    MALFORMED_UTF8("MalformedUtf8"),
    RESERVED_BITS_SET("ReservedBitsSet"),
    OUT_OF_RANGE("OutOfRange"),
    BAD_MAGIC("BadMagic"),
    UNKNOWN_TYPE("UnknownType"),
    WRONG_DIRECTION("WrongDirection"),
    UNEXPECTED_TYPE("UnexpectedType"),
    FRAME_TOO_LARGE("FrameTooLarge");

    private final String specName;

    DecodeError(String specName) {
        this.specName = specName;
    }

    public String specName() {
        return specName;
    }
}
