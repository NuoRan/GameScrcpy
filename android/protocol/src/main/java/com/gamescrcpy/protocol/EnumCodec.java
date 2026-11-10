package com.gamescrcpy.protocol;

public final class EnumCodec<E extends Enum<E> & WireEnum> {

    private final E[] constants;
    private final int width;

    private EnumCodec(Class<E> type, int width) {
        this.constants = type.getEnumConstants();
        this.width = width;
    }

    public static <E extends Enum<E> & WireEnum> EnumCodec<E> u8(Class<E> type) {
        return new EnumCodec<>(type, 1);
    }

    public static <E extends Enum<E> & WireEnum> EnumCodec<E> u16(Class<E> type) {
        return new EnumCodec<>(type, 2);
    }

    public int width() {
        return width;
    }

    public E fromWire(int value) {
        for (E constant : constants) {
            if (constant.wireValue() == value) {
                return constant;
            }
        }
        return null;
    }

    public E fromSpecName(String name) {
        for (E constant : constants) {
            if (constant.specName().equals(name)) {
                return constant;
            }
        }
        return null;
    }
}
