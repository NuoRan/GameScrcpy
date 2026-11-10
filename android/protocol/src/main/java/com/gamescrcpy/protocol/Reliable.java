package com.gamescrcpy.protocol;

public final class Reliable {

    public static final int DATAGRAM_LIMIT = 1200;
    public static final int COUNTER_SIZE = 8;
    public static final int ACK_BLOCK_SIZE = 9;
    public static final int SEGMENT_HEADER_SIZE = 6;
    public static final int CONTROL_OVERHEAD =
            Datagram.HEADER_SIZE + COUNTER_SIZE + ACK_BLOCK_SIZE + Datagram.CONTROL_TAG_SIZE;
    public static final int BULK_OVERHEAD = Datagram.HEADER_SIZE + ACK_BLOCK_SIZE;
    public static final int MAX_CONTROL_SEGMENT = DATAGRAM_LIMIT - CONTROL_OVERHEAD - SEGMENT_HEADER_SIZE;
    public static final int MAX_BULK_SEGMENT = DATAGRAM_LIMIT - BULK_OVERHEAD - SEGMENT_HEADER_SIZE;
    public static final int CONTROL_CHANNEL = 0;
    public static final int BULK_CHANNEL = 3;

    private Reliable() {
    }

    public static final class Block {
        public long ack;
        public long sack;
        public int count;
        public long seq;
        public int offset;
        public int length;

        private byte[] data;
        private int cursor;
        private int left;

        public boolean next() {
            if (left == 0) {
                return false;
            }
            --left;
            seq = ByteOrder.readU32(data, cursor);
            length = ByteOrder.readU16(data, cursor + 4);
            offset = cursor + SEGMENT_HEADER_SIZE;
            cursor = offset + length;
            return true;
        }
    }

    public static Block decodeBlock(byte[] data, int offset, int length, Block block) {
        if (length < ACK_BLOCK_SIZE) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        final int end = offset + length;
        final int count = ByteOrder.readU8(data, offset + 8);
        int at = offset + ACK_BLOCK_SIZE;
        for (int i = 0; i < count; ++i) {
            if (end - at < SEGMENT_HEADER_SIZE) {
                throw new DecodeException(DecodeError.TRUNCATED);
            }
            final int size = ByteOrder.readU16(data, at + 4);
            if (size == 0) {
                throw new DecodeException(DecodeError.OUT_OF_RANGE);
            }
            if (end - at - SEGMENT_HEADER_SIZE < size) {
                throw new DecodeException(DecodeError.TRUNCATED);
            }
            at += SEGMENT_HEADER_SIZE + size;
        }
        if (at != end) {
            throw new DecodeException(DecodeError.TRAILING_BYTES);
        }
        block.ack = ByteOrder.readU32(data, offset);
        block.sack = ByteOrder.readU32(data, offset + 4);
        block.count = count;
        block.data = data;
        block.cursor = offset + ACK_BLOCK_SIZE;
        block.left = count;
        return block;
    }

    public static long decodeControl(byte[] token, byte[] data, int length, Block block) {
        if (length < CONTROL_OVERHEAD) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        final int tagAt = length - Datagram.CONTROL_TAG_SIZE;
        final byte[] expected = Proofs.controlTag(token, data, 0, tagAt);
        int difference = 0;
        for (int i = 0; i < Datagram.CONTROL_TAG_SIZE; ++i) {
            difference |= expected[i] ^ data[tagAt + i];
        }
        if (difference != 0) {
            return 0;
        }
        final long counter = ByteOrder.readU64(data, Datagram.HEADER_SIZE);
        if (counter == 0) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        final int blockAt = Datagram.HEADER_SIZE + COUNTER_SIZE;
        decodeBlock(data, blockAt, tagAt - blockAt, block);
        return counter;
    }

    public static Block decodeBulk(byte[] data, int length, Block block) {
        if (length < BULK_OVERHEAD) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        return decodeBlock(data, Datagram.HEADER_SIZE, length - Datagram.HEADER_SIZE, block);
    }

    public static final class Writer {
        private final byte[] out;
        private int countAt;
        private int size;
        private int reserved;
        private int count;

        public Writer(byte[] out) {
            if (out.length < DATAGRAM_LIMIT) {
                throw new IllegalArgumentException("a datagram buffer needs " + DATAGRAM_LIMIT + " bytes");
            }
            this.out = out;
        }

        public byte[] buffer() {
            return out;
        }

        public Writer startControl(long connection, long counter, long ack, long sack) {
            Datagram.writeHeader(out, 0, CONTROL_CHANNEL, connection);
            ByteOrder.writeU64(out, Datagram.HEADER_SIZE, counter);
            return start(Datagram.HEADER_SIZE + COUNTER_SIZE, ack, sack, Datagram.CONTROL_TAG_SIZE);
        }

        public Writer startBulk(long connection, long ack, long sack) {
            Datagram.writeHeader(out, 0, BULK_CHANNEL, connection);
            return start(Datagram.HEADER_SIZE, ack, sack, 0);
        }

        private Writer start(int acksAt, long ack, long sack, int reserved) {
            ByteOrder.writeU32(out, acksAt, ack);
            ByteOrder.writeU32(out, acksAt + 4, sack);
            countAt = acksAt + 8;
            size = countAt + 1;
            this.reserved = reserved;
            count = 0;
            return this;
        }

        public int room() {
            final int used = size + reserved + SEGMENT_HEADER_SIZE;
            return count == 255 || used >= DATAGRAM_LIMIT ? 0 : DATAGRAM_LIMIT - used;
        }

        public boolean add(long seq, byte[] data, int offset, int length) {
            if (length == 0 || length > room()) {
                return false;
            }
            ByteOrder.writeU32(out, size, seq);
            ByteOrder.writeU16(out, size + 4, length);
            System.arraycopy(data, offset, out, size + SEGMENT_HEADER_SIZE, length);
            size += SEGMENT_HEADER_SIZE + length;
            ++count;
            return true;
        }

        public int count() {
            return count;
        }

        public int finish() {
            out[countAt] = (byte) count;
            return size;
        }

        public int finish(byte[] token) {
            out[countAt] = (byte) count;
            System.arraycopy(Proofs.controlTag(token, out, 0, size), 0, out, size, Datagram.CONTROL_TAG_SIZE);
            return size + Datagram.CONTROL_TAG_SIZE;
        }
    }
}
