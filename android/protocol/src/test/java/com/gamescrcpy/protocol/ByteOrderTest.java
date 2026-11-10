package com.gamescrcpy.protocol;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class ByteOrderTest {

    @Test
    void writesTheMostSignificantByteFirst() {
        byte[] buffer = new byte[4];
        ByteOrder.writeU32(buffer, 0, 0x11223344L);
        assertArrayEquals(new byte[] {0x11, 0x22, 0x33, 0x44}, buffer);
    }

    @Test
    void readsTheMostSignificantByteFirst() {
        byte[] buffer = {0x01, 0x23, 0x45, 0x67, (byte) 0x89, (byte) 0xAB, (byte) 0xCD, (byte) 0xEF};
        assertEquals(0x0123456789ABCDEFL, ByteOrder.readU64(buffer, 0));
    }

    @Test
    void unsignedReadsNeverSignExtend() {
        byte[] buffer = {(byte) 0xFF, (byte) 0xFF, (byte) 0xFF, (byte) 0xFF};
        assertEquals(0xFF, ByteOrder.readU8(buffer, 0));
        assertEquals(0xFFFF, ByteOrder.readU16(buffer, 0));
        assertEquals(0xFFFF_FFFFL, ByteOrder.readU32(buffer, 0));
    }

    @Test
    void roundTripsEveryWidthAtAnOffset() {
        byte[] buffer = new byte[16];
        ByteOrder.writeU16(buffer, 1, 0xBEEF);
        ByteOrder.writeU32(buffer, 3, 0xDEADBEEFL);
        ByteOrder.writeU64(buffer, 7, 0x8000_0000_0000_0001L);
        assertEquals(0xBEEF, ByteOrder.readU16(buffer, 1));
        assertEquals(0xDEADBEEFL, ByteOrder.readU32(buffer, 3));
        assertEquals(0x8000_0000_0000_0001L, ByteOrder.readU64(buffer, 7));
    }
}
