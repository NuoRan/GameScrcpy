package com.gamescrcpy.server.uhid;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.jupiter.api.Test;

class UhidEventsTest {

    static byte[] bytes(int... values) {
        final byte[] result = new byte[values.length];
        for (int i = 0; i < values.length; ++i) {
            result[i] = (byte) values[i];
        }
        return result;
    }

    @Test
    void create2PacksNameIdsBusAndDescriptorInTheKernelLayout() {
        final byte[] event = UhidEvents.create2("Pad", 3, 0x1209, 0x0001, bytes(0x05, 0x01));
        final int name = 4;
        final int descriptorSize = name + 128 + 64 + 64;
        final int bus = descriptorSize + 2;
        final int vendor = bus + 2;
        final int product = vendor + 4;
        final int version = product + 4;
        final int country = version + 4;
        final int descriptor = country + 4;
        assertEquals(280, descriptor);
        final byte[] expected = new byte[descriptor + 2];
        expected[0] = 11;
        expected[name] = 'P';
        expected[name + 1] = 'a';
        expected[name + 2] = 'd';
        expected[descriptorSize] = 2;
        expected[bus] = 3;
        expected[vendor] = 0x09;
        expected[vendor + 1] = 0x12;
        expected[product] = 0x01;
        expected[descriptor] = 0x05;
        expected[descriptor + 1] = 0x01;
        assertArrayEquals(expected, event);
    }

    @Test
    void aLongNameKeepsItsTerminatingZero() {
        final byte[] event = UhidEvents.create2("x".repeat(200), 3, 1, 2, bytes(0x05, 0x01));
        assertEquals('x', event[4 + 126]);
        assertEquals(0, event[4 + 127]);
    }

    @Test
    void create2CarriesTheBusTheClientAskedFor() {
        final int bus = 4 + 128 + 64 + 64 + 2;
        final byte[] bluetooth = UhidEvents.create2("Xbox Wireless Controller", 5, 0x045E, 0x0B13, bytes(0x05, 0x01));
        assertEquals(5, bluetooth[bus]);
        assertEquals(0, bluetooth[bus + 1]);
        assertEquals(0x5E, bluetooth[bus + 2] & 0xFF);
        assertEquals(0x04, bluetooth[bus + 3]);
        assertEquals(0x13, bluetooth[bus + 6]);
        assertEquals(0x0B, bluetooth[bus + 7]);
        assertEquals(0xFF, UhidEvents.create2("Odd", 0xFF, 1, 2, bytes(0x05, 0x01))[bus] & 0xFF);
    }

    @Test
    void input2CarriesTheReportAfterItsSize() {
        assertArrayEquals(bytes(12, 0, 0, 0, 3, 0, 1, 2, 3), UhidEvents.input2(bytes(1, 2, 3)));
    }

    @Test
    void destroyIsTheEventTypeAlone() {
        assertArrayEquals(bytes(1, 0, 0, 0), UhidEvents.destroy());
    }

    @Test
    void repliesNameTheRequestTheyAnswer() {
        assertArrayEquals(bytes(10, 0, 0, 0, 77, 0, 0, 0, 0, 0, 2, 0, 2, 16),
                UhidEvents.getReportReply(77, 0, bytes(2, 16)));
        assertArrayEquals(bytes(10, 0, 0, 0, 0x2C, 0x01, 0, 0, 5, 0, 0, 0),
                UhidEvents.getReportReply(300, UhidEvents.EIO, new byte[0]));
        assertArrayEquals(bytes(14, 0, 0, 0, 80, 0, 0, 0, 0, 0), UhidEvents.setReportReply(80, 0));
    }

    @Test
    void kernelEventsAreReadInTheKernelLayout() {
        final byte[] request = bytes(9, 0, 0, 0, 0x2C, 0x01, 0, 0, 2, 0);
        assertEquals(UhidEvents.GET_REPORT, UhidEvents.type(request));
        assertEquals(300, UhidEvents.requestId(request));
        assertEquals(2, UhidEvents.reportNumber(request));
        assertEquals(UhidEvents.FEATURE_REPORT, UhidEvents.reportType(request));

        final ByteBuffer output = ByteBuffer.allocate(UhidEvents.OUTPUT_EVENT_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        output.putInt(UhidEvents.OUTPUT).put(bytes(7, 8, 9));
        output.putShort(UhidEvents.OUTPUT_SIZE_AT, (short) 3);
        assertEquals(4100, UhidEvents.OUTPUT_SIZE_AT);
        assertEquals(UhidEvents.OUTPUT, UhidEvents.type(output.array()));
        assertArrayEquals(bytes(7, 8, 9), UhidEvents.outputData(output.array()));
    }
}
