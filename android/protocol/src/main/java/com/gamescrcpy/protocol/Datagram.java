package com.gamescrcpy.protocol;

public final class Datagram {

    public static final int HEADER_SIZE = 8;
    public static final int MAX_SIZE = 1280;
    public static final int MAGIC = 0x4753;
    public static final int HANDSHAKE_CHANNEL = 15;
    public static final int CONTROL_TAG_SIZE = 16;

    private Datagram() {
    }

    public static final class Header {
        public final int channel;
        public final long connection;

        public Header(int channel, long connection) {
            this.channel = channel;
            this.connection = connection;
        }
    }

    public static boolean isChannel(int channel) {
        return (channel >= 0 && channel <= 3) || channel == HANDSHAKE_CHANNEL;
    }

    public static void writeHeader(byte[] out, int offset, int channel, long connection) {
        if (!isChannel(channel) || connection < 0 || connection > 0xFFFF_FFFFL) {
            throw new EncodeException(EncodeError.VALUE_OUT_OF_RANGE);
        }
        if (out.length - offset < HEADER_SIZE) {
            throw new EncodeException(EncodeError.BUFFER_TOO_SMALL);
        }
        ByteOrder.writeU16(out, offset, MAGIC);
        ByteOrder.writeU8(out, offset + 2, channel);
        ByteOrder.writeU8(out, offset + 3, 0);
        ByteOrder.writeU32(out, offset + 4, connection);
    }

    public static Header decodeHeader(byte[] data, int offset, int length) {
        if (length < HEADER_SIZE) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        if (ByteOrder.readU16(data, offset) != MAGIC) {
            throw new DecodeException(DecodeError.BAD_MAGIC);
        }
        final int channel = ByteOrder.readU8(data, offset + 2);
        if (!isChannel(channel)) {
            throw new DecodeException(DecodeError.INVALID_ENUM);
        }
        if (ByteOrder.readU8(data, offset + 3) != 0) {
            throw new DecodeException(DecodeError.RESERVED_BITS_SET);
        }
        if (length > MAX_SIZE) {
            throw new DecodeException(DecodeError.FRAME_TOO_LARGE);
        }
        return new Header(channel, ByteOrder.readU32(data, offset + 4));
    }
}
