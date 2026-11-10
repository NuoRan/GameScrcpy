package com.gamescrcpy.protocol;

public final class ByteOrder {

    private ByteOrder() {
    }

    public static int readU8(byte[] buffer, int offset) {
        return buffer[offset] & 0xFF;
    }

    public static int readU16(byte[] buffer, int offset) {
        return (readU8(buffer, offset) << 8) | readU8(buffer, offset + 1);
    }

    public static long readU32(byte[] buffer, int offset) {
        return ((long) readU16(buffer, offset) << 16) | readU16(buffer, offset + 2);
    }

    public static long readU64(byte[] buffer, int offset) {
        return (readU32(buffer, offset) << 32) | readU32(buffer, offset + 4);
    }

    public static void writeU8(byte[] buffer, int offset, int value) {
        buffer[offset] = (byte) value;
    }

    public static void writeU16(byte[] buffer, int offset, int value) {
        buffer[offset] = (byte) (value >>> 8);
        buffer[offset + 1] = (byte) value;
    }

    public static void writeU32(byte[] buffer, int offset, long value) {
        writeU16(buffer, offset, (int) (value >>> 16));
        writeU16(buffer, offset + 2, (int) value);
    }

    public static void writeU64(byte[] buffer, int offset, long value) {
        writeU32(buffer, offset, value >>> 32);
        writeU32(buffer, offset + 4, value);
    }
}
