package com.gamescrcpy.server.uhid;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

final class UhidEvents {

    static final int DESTROY = 1;
    static final int START = 2;
    static final int STOP = 3;
    static final int OPEN = 4;
    static final int CLOSE = 5;
    static final int OUTPUT = 6;
    static final int GET_REPORT = 9;
    static final int GET_REPORT_REPLY = 10;
    static final int CREATE2 = 11;
    static final int INPUT2 = 12;
    static final int SET_REPORT = 13;
    static final int SET_REPORT_REPLY = 14;

    static final int FEATURE_REPORT = 0;
    static final int OUTPUT_REPORT = 1;
    static final int INPUT_REPORT = 2;

    static final int DATA_MAX = 4096;
    static final int EIO = 5;

    static final int TYPE_SIZE = 4;
    static final int NAME_SIZE = 128;
    static final int PHYS_SIZE = 64;
    static final int UNIQ_SIZE = 64;
    static final int CREATE2_DESCRIPTOR_AT = TYPE_SIZE + NAME_SIZE + PHYS_SIZE + UNIQ_SIZE + 2 + 2 + 4 * 4;
    static final int OUTPUT_SIZE_AT = TYPE_SIZE + DATA_MAX;
    static final int OUTPUT_EVENT_SIZE = OUTPUT_SIZE_AT + 2 + 1;
    static final int REPORT_REQUEST_SIZE = TYPE_SIZE + 4 + 1 + 1;

    private UhidEvents() {
    }

    static byte[] create2(String name, int bus, int vendor, int product, byte[] descriptor) {
        final byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
        final ByteBuffer event = buffer(CREATE2_DESCRIPTOR_AT + descriptor.length);
        event.putInt(CREATE2);
        event.put(nameBytes, 0, Math.min(nameBytes.length, NAME_SIZE - 1));
        event.position(TYPE_SIZE + NAME_SIZE + PHYS_SIZE + UNIQ_SIZE);
        event.putShort((short) descriptor.length);
        event.putShort((short) bus);
        event.putInt(vendor);
        event.putInt(product);
        event.putInt(0);
        event.putInt(0);
        event.put(descriptor);
        return event.array();
    }

    static byte[] input2(byte[] report) {
        final ByteBuffer event = buffer(TYPE_SIZE + 2 + report.length);
        event.putInt(INPUT2);
        event.putShort((short) report.length);
        event.put(report);
        return event.array();
    }

    static byte[] destroy() {
        return buffer(TYPE_SIZE).putInt(DESTROY).array();
    }

    static byte[] getReportReply(int requestId, int error, byte[] data) {
        final ByteBuffer event = buffer(TYPE_SIZE + 4 + 2 + 2 + data.length);
        event.putInt(GET_REPORT_REPLY);
        event.putInt(requestId);
        event.putShort((short) error);
        event.putShort((short) data.length);
        event.put(data);
        return event.array();
    }

    static byte[] setReportReply(int requestId, int error) {
        final ByteBuffer event = buffer(TYPE_SIZE + 4 + 2);
        event.putInt(SET_REPORT_REPLY);
        event.putInt(requestId);
        event.putShort((short) error);
        return event.array();
    }

    static int type(byte[] event) {
        return wrap(event).getInt(0);
    }

    static int requestId(byte[] event) {
        return wrap(event).getInt(TYPE_SIZE);
    }

    static int reportNumber(byte[] event) {
        return event[TYPE_SIZE + 4] & 0xFF;
    }

    static int reportType(byte[] event) {
        return event[TYPE_SIZE + 5] & 0xFF;
    }

    static byte[] outputData(byte[] event) {
        final int size = Math.min(wrap(event).getShort(OUTPUT_SIZE_AT) & 0xFFFF, DATA_MAX);
        return Arrays.copyOfRange(event, TYPE_SIZE, TYPE_SIZE + size);
    }

    private static ByteBuffer buffer(int size) {
        return ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN);
    }

    private static ByteBuffer wrap(byte[] event) {
        return ByteBuffer.wrap(event).order(ByteOrder.LITTLE_ENDIAN);
    }
}
