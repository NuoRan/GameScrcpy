package com.gamescrcpy.protocol;

import java.util.Arrays;
import java.util.function.IntBinaryOperator;

public final class DirectMedia {

    public static final int VIDEO_CHANNEL = 1;
    public static final int AUDIO_CHANNEL = 2;
    public static final int SHARD_HEADER_SIZE = 16;
    public static final int MAX_SHARD_SIZE = Reliable.DATAGRAM_LIMIT - Datagram.HEADER_SIZE - SHARD_HEADER_SIZE;
    public static final int SHARDS_PER_GROUP = 64;
    public static final int MAX_PACKET_SIZE = (16 << 20) + 20;
    public static final int AUDIO_HEADER_SIZE = 5;
    public static final int CLOSED_SIZE = Datagram.HEADER_SIZE + 1;

    private DirectMedia() {
    }

    public static int recommendedShardSize(int packetSize) {
        final int count = (packetSize + MAX_SHARD_SIZE - 1) / MAX_SHARD_SIZE;
        return (packetSize + count - 1) / count;
    }

    public static int writeClosed(byte[] out, int channel, long connection) {
        Datagram.writeHeader(out, 0, channel, connection);
        out[Datagram.HEADER_SIZE] = 0;
        return CLOSED_SIZE;
    }

    public static boolean isClosed(byte[] data, int length) {
        return length == CLOSED_SIZE && data[Datagram.HEADER_SIZE] == 0;
    }

    public interface Sink {
        void send(byte[] datagram, int length);
    }

    public static final class VideoSlicer {
        private final byte[] datagram = new byte[Reliable.DATAGRAM_LIMIT];
        private byte[][] shards = new byte[0][];

        public void slice(long connection, long number, byte[] packet, int offset, int length, int shardSize,
                          IntBinaryOperator parity, Sink sink) {
            if (length < 1 || length > MAX_PACKET_SIZE || shardSize < 1 || shardSize > MAX_SHARD_SIZE) {
                throw new EncodeException(EncodeError.VALUE_OUT_OF_RANGE);
            }
            final int count = (length + shardSize - 1) / shardSize;
            for (int group = 0; group * SHARDS_PER_GROUP < count; ++group) {
                final int first = group * SHARDS_PER_GROUP;
                final int k = Math.min(SHARDS_PER_GROUP, count - first);
                final int m = parity.applyAsInt(group, k);
                if (m < 0 || k + m > ReedSolomon.MAX_SHARDS) {
                    throw new EncodeException(EncodeError.VALUE_OUT_OF_RANGE);
                }
                for (int i = 0; i < k; ++i) {
                    final int at = (first + i) * shardSize;
                    send(connection, number, length, shardSize, group, i, k, m, packet, offset + at,
                            Math.min(shardSize, length - at), sink);
                }
                if (m == 0) {
                    continue;
                }
                prepare(k + m, shardSize);
                for (int i = 0; i < k; ++i) {
                    final int at = (first + i) * shardSize;
                    final int size = Math.min(shardSize, length - at);
                    System.arraycopy(packet, offset + at, shards[i], 0, size);
                    Arrays.fill(shards[i], size, shardSize, (byte) 0);
                }
                final byte[][] groupShards = Arrays.copyOf(shards, k + m);
                new ReedSolomon(k, m).encode(groupShards, shardSize);
                for (int i = k; i < k + m; ++i) {
                    send(connection, number, length, shardSize, group, i, k, m, groupShards[i], 0, shardSize, sink);
                }
            }
        }

        private void prepare(int count, int shardSize) {
            if (shards.length < count) {
                shards = Arrays.copyOf(shards, count);
            }
            for (int i = 0; i < count; ++i) {
                if (shards[i] == null || shards[i].length != shardSize) {
                    shards[i] = new byte[shardSize];
                }
            }
        }

        private void send(long connection, long number, int packetSize, int shardSize, int group, int index, int k,
                          int m, byte[] bytes, int offset, int size, Sink sink) {
            Datagram.writeHeader(datagram, 0, VIDEO_CHANNEL, connection);
            final int at = Datagram.HEADER_SIZE;
            ByteOrder.writeU32(datagram, at, number);
            ByteOrder.writeU32(datagram, at + 4, packetSize);
            ByteOrder.writeU16(datagram, at + 8, shardSize);
            ByteOrder.writeU16(datagram, at + 10, group);
            ByteOrder.writeU8(datagram, at + 12, index);
            ByteOrder.writeU8(datagram, at + 13, k);
            ByteOrder.writeU8(datagram, at + 14, m);
            ByteOrder.writeU8(datagram, at + 15, 0);
            System.arraycopy(bytes, offset, datagram, at + SHARD_HEADER_SIZE, size);
            sink.send(datagram, at + SHARD_HEADER_SIZE + size);
        }
    }

    public static int writeAudio(byte[] out, long connection, long number, byte[] current, int currentLength,
                                 byte[] previous, int previousLength) {
        final int single = Datagram.HEADER_SIZE + AUDIO_HEADER_SIZE + 2 + currentLength;
        if (currentLength < 1 || single > Reliable.DATAGRAM_LIMIT) {
            throw new EncodeException(EncodeError.VALUE_OUT_OF_RANGE);
        }
        final boolean both = previous != null && number > 0 && single + 2 + previousLength <= Reliable.DATAGRAM_LIMIT;
        Datagram.writeHeader(out, 0, AUDIO_CHANNEL, connection);
        ByteOrder.writeU32(out, Datagram.HEADER_SIZE, number);
        ByteOrder.writeU8(out, Datagram.HEADER_SIZE + 4, both ? 2 : 1);
        int at = Datagram.HEADER_SIZE + AUDIO_HEADER_SIZE;
        ByteOrder.writeU16(out, at, currentLength);
        System.arraycopy(current, 0, out, at + 2, currentLength);
        at += 2 + currentLength;
        if (both) {
            ByteOrder.writeU16(out, at, previousLength);
            System.arraycopy(previous, 0, out, at + 2, previousLength);
            at += 2 + previousLength;
        }
        return at;
    }

    public static final class VideoShard {
        public long packet;
        public int packetSize;
        public int shardSize;
        public int group;
        public int index;
        public int k;
        public int m;
        public int offset;
        public int size;
    }

    public static VideoShard decodeVideoShard(byte[] data, int length) {
        if (length < Datagram.HEADER_SIZE + SHARD_HEADER_SIZE) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        final int at = Datagram.HEADER_SIZE;
        final VideoShard shard = new VideoShard();
        shard.packet = ByteOrder.readU32(data, at);
        final long packetSize = ByteOrder.readU32(data, at + 4);
        shard.shardSize = ByteOrder.readU16(data, at + 8);
        shard.group = ByteOrder.readU16(data, at + 10);
        shard.index = ByteOrder.readU8(data, at + 12);
        shard.k = ByteOrder.readU8(data, at + 13);
        shard.m = ByteOrder.readU8(data, at + 14);
        if (ByteOrder.readU8(data, at + 15) != 0) {
            throw new DecodeException(DecodeError.RESERVED_BITS_SET);
        }
        if (packetSize == 0 || packetSize > MAX_PACKET_SIZE || shard.shardSize == 0) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        shard.packetSize = (int) packetSize;
        final long count = (packetSize + shard.shardSize - 1) / shard.shardSize;
        final long groups = (count + SHARDS_PER_GROUP - 1) / SHARDS_PER_GROUP;
        final long first = (long) shard.group * SHARDS_PER_GROUP;
        if (shard.group >= groups || shard.k != Math.min(SHARDS_PER_GROUP, count - first)
                || shard.k + shard.m > ReedSolomon.MAX_SHARDS || shard.index >= shard.k + shard.m) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        final long expected = shard.index < shard.k
                ? Math.min(shard.shardSize, packetSize - (first + shard.index) * shard.shardSize) : shard.shardSize;
        final int size = length - at - SHARD_HEADER_SIZE;
        if (size < expected) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        if (size > expected) {
            throw new DecodeException(DecodeError.TRAILING_BYTES);
        }
        shard.offset = at + SHARD_HEADER_SIZE;
        shard.size = size;
        return shard;
    }

    public static final class AudioDatagram {
        public long packet;
        public int count;
        public final int[] offsets = new int[2];
        public final int[] sizes = new int[2];
    }

    public static AudioDatagram decodeAudio(byte[] data, int length) {
        if (length < Datagram.HEADER_SIZE + AUDIO_HEADER_SIZE) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
        final AudioDatagram audio = new AudioDatagram();
        audio.packet = ByteOrder.readU32(data, Datagram.HEADER_SIZE);
        audio.count = ByteOrder.readU8(data, Datagram.HEADER_SIZE + 4);
        if ((audio.count != 1 && audio.count != 2) || (audio.count == 2 && audio.packet == 0)) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        int at = Datagram.HEADER_SIZE + AUDIO_HEADER_SIZE;
        for (int i = 0; i < audio.count; ++i) {
            if (length - at < 2) {
                throw new DecodeException(DecodeError.TRUNCATED);
            }
            final int size = ByteOrder.readU16(data, at);
            if (size == 0) {
                throw new DecodeException(DecodeError.OUT_OF_RANGE);
            }
            if (length - at - 2 < size) {
                throw new DecodeException(DecodeError.TRUNCATED);
            }
            audio.offsets[i] = at + 2;
            audio.sizes[i] = size;
            at += 2 + size;
        }
        if (at != length) {
            throw new DecodeException(DecodeError.TRAILING_BYTES);
        }
        return audio;
    }
}
