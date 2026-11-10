package com.gamescrcpy.protocol;

import org.json.JSONArray;
import org.json.JSONObject;

final class Json {

    private Json() {
    }

    static JSONObject toJson(Struct struct) {
        final JSONObject out = new JSONObject();
        struct.visit(new Writer(out));
        return out;
    }

    static <T extends Struct> T fromJson(T struct, JSONObject in) {
        struct.visit(new Reader(in));
        return struct;
    }

    static String toHex(byte[] bytes, int offset, int length) {
        final StringBuilder text = new StringBuilder(length * 2);
        for (int i = offset; i < offset + length; ++i) {
            text.append(Character.forDigit((bytes[i] >> 4) & 0xF, 16));
            text.append(Character.forDigit(bytes[i] & 0xF, 16));
        }
        return text.toString();
    }

    static String toHex(byte[] bytes) {
        return toHex(bytes, 0, bytes.length);
    }

    static byte[] fromHex(String hex) {
        final byte[] bytes = new byte[hex.length() / 2];
        for (int i = 0; i < bytes.length; ++i) {
            bytes[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        }
        return bytes;
    }

    private static final class Writer implements FieldVisitor {
        private final JSONObject out;

        Writer(JSONObject out) {
            this.out = out;
        }

        @Override public void magic(long value) { }
        @Override public int u8(String name, int value) { out.put(name, value); return value; }
        @Override public int u16(String name, int value) { out.put(name, value); return value; }
        @Override public long u32(String name, long value) { out.put(name, value); return value; }
        @Override public long u64(String name, long value) {
            out.put(name, Long.toUnsignedString(value));
            return value;
        }
        @Override public int i16(String name, int value) { out.put(name, value); return value; }
        @Override public long i64(String name, long value) { out.put(name, Long.toString(value)); return value; }
        @Override public int u8Range(String name, int value, int min, int max) { out.put(name, value); return value; }
        @Override public int u16Range(String name, int value, int min, int max) { out.put(name, value); return value; }
        @Override public long u32Mask(String name, long value, long allowedBits) { out.put(name, value); return value; }
        @Override public boolean bool(String name, boolean value) { out.put(name, value); return value; }

        @Override
        public <E extends Enum<E> & WireEnum> E enumeration(String name, E value, EnumCodec<E> codec) {
            out.put(name, value.specName());
            return value;
        }

        @Override public byte[] bytes(String name, byte[] value, int size) { out.put(name, toHex(value)); return value; }
        @Override public String string(String name, String value, int prefixBytes, int maxBytes) {
            out.put(name, value);
            return value;
        }
        @Override public byte[] blob(String name, byte[] value, int prefixBytes, int maxBytes) {
            out.put(name, toHex(value));
            return value;
        }

        @Override
        public int flags8(int value, String... names) {
            for (int i = 0; i < names.length; ++i) {
                out.put(names[i], (value & (1 << i)) != 0);
            }
            return value;
        }

        @Override
        public <T extends Struct> T object(String name, T value) {
            out.put(name, toJson(value));
            return value;
        }

        @Override
        public <T extends Struct> void list(String name, StructList<T> list, int prefixBytes, int min,
                                            int max) {
            final JSONArray array = new JSONArray();
            for (T item : list) {
                array.put(toJson(item));
            }
            out.put(name, array);
        }
    }

    private static final class Reader implements FieldVisitor {
        private final JSONObject in;

        Reader(JSONObject in) {
            this.in = in;
        }

        @Override public void magic(long value) { }
        @Override public int u8(String name, int value) { return in.getInt(name); }
        @Override public int u16(String name, int value) { return in.getInt(name); }
        @Override public long u32(String name, long value) { return in.getLong(name); }
        @Override public long u64(String name, long value) { return Long.parseUnsignedLong(in.getString(name)); }
        @Override public int i16(String name, int value) { return in.getInt(name); }
        @Override public long i64(String name, long value) { return Long.parseLong(in.getString(name)); }
        @Override public int u8Range(String name, int value, int min, int max) { return in.getInt(name); }
        @Override public int u16Range(String name, int value, int min, int max) { return in.getInt(name); }
        @Override public long u32Mask(String name, long value, long allowedBits) { return in.getLong(name); }
        @Override public boolean bool(String name, boolean value) { return in.getBoolean(name); }

        @Override
        public <E extends Enum<E> & WireEnum> E enumeration(String name, E value, EnumCodec<E> codec) {
            final E constant = codec.fromSpecName(in.getString(name));
            if (constant == null) {
                throw new IllegalArgumentException("unknown " + name + ": " + in.getString(name));
            }
            return constant;
        }

        @Override public byte[] bytes(String name, byte[] value, int size) { return fromHex(in.getString(name)); }
        @Override public String string(String name, String value, int prefixBytes, int maxBytes) {
            return in.getString(name);
        }
        @Override public byte[] blob(String name, byte[] value, int prefixBytes, int maxBytes) {
            return fromHex(in.getString(name));
        }

        @Override
        public int flags8(int value, String... names) {
            int bits = 0;
            for (int i = 0; i < names.length; ++i) {
                if (in.getBoolean(names[i])) {
                    bits |= 1 << i;
                }
            }
            return bits;
        }

        @Override
        public <T extends Struct> T object(String name, T value) {
            value.visit(new Reader(in.getJSONObject(name)));
            return value;
        }

        @Override
        public <T extends Struct> void list(String name, StructList<T> list, int prefixBytes, int min,
                                            int max) {
            final JSONArray array = in.getJSONArray(name);
            list.clear();
            for (int i = 0; i < array.length(); ++i) {
                list.appendForDecode().visit(new Reader(array.getJSONObject(i)));
            }
        }
    }
}
