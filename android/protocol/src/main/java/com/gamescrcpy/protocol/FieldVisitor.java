package com.gamescrcpy.protocol;

public interface FieldVisitor {

    void magic(long value);

    int u8(String name, int value);

    int u16(String name, int value);

    long u32(String name, long value);

    long u64(String name, long value);

    int i16(String name, int value);

    long i64(String name, long value);

    int u8Range(String name, int value, int min, int max);

    int u16Range(String name, int value, int min, int max);

    long u32Mask(String name, long value, long allowedBits);

    boolean bool(String name, boolean value);

    <E extends Enum<E> & WireEnum> E enumeration(String name, E value, EnumCodec<E> codec);

    byte[] bytes(String name, byte[] value, int size);

    String string(String name, String value, int prefixBytes, int maxBytes);

    byte[] blob(String name, byte[] value, int prefixBytes, int maxBytes);

    int flags8(int value, String... names);

    <T extends Struct> T object(String name, T value);

    <T extends Struct> void list(String name, StructList<T> list, int prefixBytes, int min, int max);

    default String str8(String name, String value, int maxBytes) {
        return string(name, value, 1, maxBytes);
    }

    default String str16(String name, String value, int maxBytes) {
        return string(name, value, 2, maxBytes);
    }

    default String str32(String name, String value, int maxBytes) {
        return string(name, value, 4, maxBytes);
    }

    default byte[] blob8(String name, byte[] value, int maxBytes) {
        return blob(name, value, 1, maxBytes);
    }

    default byte[] blob16(String name, byte[] value, int maxBytes) {
        return blob(name, value, 2, maxBytes);
    }

    default byte[] blob32(String name, byte[] value, int maxBytes) {
        return blob(name, value, 4, maxBytes);
    }

    default <T extends Struct> void list8(String name, StructList<T> list, int min, int max) {
        list(name, list, 1, min, max);
    }

    default <T extends Struct> void list16(String name, StructList<T> list, int min, int max) {
        list(name, list, 2, min, max);
    }
}
