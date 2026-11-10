package com.gamescrcpy.protocol;

final class EncodeVisitor implements FieldVisitor {

    private final byte[] out;
    private final int start;
    private int position;

    private EncodeVisitor(byte[] out, int offset) {
        this.out = out;
        this.start = offset;
        this.position = offset;
    }

    static int measure(Struct struct) {
        final EncodeVisitor counter = new EncodeVisitor(null, 0);
        struct.visit(counter);
        return counter.position;
    }

    static int write(Struct struct, byte[] out, int offset) {
        final EncodeVisitor writer = new EncodeVisitor(out, offset);
        struct.visit(writer);
        return writer.position - writer.start;
    }

    @Override
    public void magic(long value) {
        put(value, 4);
    }

    @Override
    public int u8(String name, int value) {
        require(value >= 0 && value <= 0xFF);
        put(value, 1);
        return value;
    }

    @Override
    public int u16(String name, int value) {
        require(value >= 0 && value <= 0xFFFF);
        put(value, 2);
        return value;
    }

    @Override
    public long u32(String name, long value) {
        require(value >= 0 && value <= 0xFFFF_FFFFL);
        put(value, 4);
        return value;
    }

    @Override
    public long u64(String name, long value) {
        put(value, 8);
        return value;
    }

    @Override
    public int i16(String name, int value) {
        require(value >= Short.MIN_VALUE && value <= Short.MAX_VALUE);
        put(value & 0xFFFF, 2);
        return value;
    }

    @Override
    public long i64(String name, long value) {
        put(value, 8);
        return value;
    }

    @Override
    public int u8Range(String name, int value, int min, int max) {
        require(value >= min && value <= max);
        put(value, 1);
        return value;
    }

    @Override
    public int u16Range(String name, int value, int min, int max) {
        require(value >= min && value <= max);
        put(value, 2);
        return value;
    }

    @Override
    public long u32Mask(String name, long value, long allowedBits) {
        require((value & ~allowedBits) == 0);
        put(value, 4);
        return value;
    }

    @Override
    public boolean bool(String name, boolean value) {
        put(value ? 1 : 0, 1);
        return value;
    }

    @Override
    public <E extends Enum<E> & WireEnum> E enumeration(String name, E value, EnumCodec<E> codec) {
        require(value != null);
        put(value.wireValue(), codec.width());
        return value;
    }

    @Override
    public byte[] bytes(String name, byte[] value, int size) {
        require(value != null && value.length == size);
        putRaw(value);
        return value;
    }

    @Override
    public String string(String name, String value, int prefixBytes, int maxBytes) {
        final byte[] data = Utf8.encode(value);
        putPrefixed(data, prefixBytes, maxBytes);
        return value;
    }

    @Override
    public byte[] blob(String name, byte[] value, int prefixBytes, int maxBytes) {
        putPrefixed(value, prefixBytes, maxBytes);
        return value;
    }

    @Override
    public int flags8(int value, String... names) {
        require((value & ~((1 << names.length) - 1)) == 0);
        put(value, 1);
        return value;
    }

    @Override
    public <T extends Struct> T object(String name, T value) {
        value.visit(this);
        return value;
    }

    @Override
    public <T extends Struct> void list(String name, StructList<T> list, int prefixBytes, int min,
                                        int max) {
        if (list.size() > max) {
            throw new EncodeException(EncodeError.FIELD_TOO_LONG);
        }
        require(list.size() >= min);
        put(list.size(), prefixBytes);
        for (T item : list) {
            item.visit(this);
        }
    }

    private static void require(boolean valid) {
        if (!valid) {
            throw new EncodeException(EncodeError.VALUE_OUT_OF_RANGE);
        }
    }

    private void put(long value, int width) {
        if (out != null) {
            if (out.length - position < width) {
                throw new EncodeException(EncodeError.BUFFER_TOO_SMALL);
            }
            for (int i = 0; i < width; ++i) {
                out[position + i] = (byte) (value >>> (8 * (width - 1 - i)));
            }
        }
        position += width;
    }

    private void putRaw(byte[] data) {
        if (out != null) {
            if (out.length - position < data.length) {
                throw new EncodeException(EncodeError.BUFFER_TOO_SMALL);
            }
            System.arraycopy(data, 0, out, position, data.length);
        }
        position += data.length;
    }

    private void putPrefixed(byte[] data, int prefixBytes, int maxBytes) {
        if (data.length > maxBytes) {
            throw new EncodeException(EncodeError.FIELD_TOO_LONG);
        }
        put(data.length, prefixBytes);
        putRaw(data);
    }
}
