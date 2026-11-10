package com.gamescrcpy.protocol;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

final class DecodeVisitor implements FieldVisitor {

    private final byte[] in;
    private final int end;
    private int position;

    DecodeVisitor(byte[] in, int offset, int length) {
        this.in = in;
        this.position = offset;
        this.end = offset + length;
    }

    int remaining() {
        return end - position;
    }

    @Override
    public void magic(long value) {
        if (take(4) != value) {
            throw new DecodeException(DecodeError.BAD_MAGIC);
        }
    }

    @Override
    public int u8(String name, int value) {
        return (int) take(1);
    }

    @Override
    public int u16(String name, int value) {
        return (int) take(2);
    }

    @Override
    public long u32(String name, long value) {
        return take(4);
    }

    @Override
    public long u64(String name, long value) {
        return take(8);
    }

    @Override
    public int i16(String name, int value) {
        return (short) take(2);
    }

    @Override
    public long i64(String name, long value) {
        return take(8);
    }

    @Override
    public int u8Range(String name, int value, int min, int max) {
        return inRange((int) take(1), min, max);
    }

    @Override
    public int u16Range(String name, int value, int min, int max) {
        return inRange((int) take(2), min, max);
    }

    @Override
    public long u32Mask(String name, long value, long allowedBits) {
        final long bits = take(4);
        if ((bits & ~allowedBits) != 0) {
            throw new DecodeException(DecodeError.RESERVED_BITS_SET);
        }
        return bits;
    }

    @Override
    public boolean bool(String name, boolean value) {
        final long raw = take(1);
        if (raw > 1) {
            throw new DecodeException(DecodeError.INVALID_BOOL);
        }
        return raw == 1;
    }

    @Override
    public <E extends Enum<E> & WireEnum> E enumeration(String name, E value, EnumCodec<E> codec) {
        final E decoded = codec.fromWire((int) take(codec.width()));
        if (decoded == null) {
            throw new DecodeException(DecodeError.INVALID_ENUM);
        }
        return decoded;
    }

    @Override
    public byte[] bytes(String name, byte[] value, int size) {
        requireAvailable(size);
        final byte[] data = Arrays.copyOfRange(in, position, position + size);
        position += size;
        return data;
    }

    @Override
    public String string(String name, String value, int prefixBytes, int maxBytes) {
        final int length = prefixedLength(prefixBytes, maxBytes);
        if (!Utf8.isValid(in, position, length)) {
            throw new DecodeException(DecodeError.MALFORMED_UTF8);
        }
        final String text = new String(in, position, length, StandardCharsets.UTF_8);
        position += length;
        return text;
    }

    @Override
    public byte[] blob(String name, byte[] value, int prefixBytes, int maxBytes) {
        final int length = prefixedLength(prefixBytes, maxBytes);
        final byte[] data = Arrays.copyOfRange(in, position, position + length);
        position += length;
        return data;
    }

    @Override
    public int flags8(int value, String... names) {
        final int bits = (int) take(1);
        if ((bits & ~((1 << names.length) - 1)) != 0) {
            throw new DecodeException(DecodeError.RESERVED_BITS_SET);
        }
        return bits;
    }

    @Override
    public <T extends Struct> T object(String name, T value) {
        value.visit(this);
        return value;
    }

    @Override
    public <T extends Struct> void list(String name, StructList<T> list, int prefixBytes, int min,
                                        int max) {
        final long count = take(prefixBytes);
        if (count > max) {
            throw new DecodeException(DecodeError.LENGTH_EXCEEDED);
        }
        if (count < min) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        list.clear();
        for (long i = 0; i < count; ++i) {
            list.appendForDecode().visit(this);
        }
    }

    private long take(int width) {
        requireAvailable(width);
        long value = 0;
        for (int i = 0; i < width; ++i) {
            value = (value << 8) | (in[position++] & 0xFF);
        }
        return value;
    }

    private void requireAvailable(long count) {
        if (end - position < count) {
            throw new DecodeException(DecodeError.TRUNCATED);
        }
    }

    private static int inRange(int value, int min, int max) {
        if (value < min || value > max) {
            throw new DecodeException(DecodeError.OUT_OF_RANGE);
        }
        return value;
    }

    private int prefixedLength(int prefixBytes, int maxBytes) {
        final long length = take(prefixBytes);
        if (length > maxBytes) {
            throw new DecodeException(DecodeError.LENGTH_EXCEEDED);
        }
        requireAvailable(length);
        return (int) length;
    }
}
