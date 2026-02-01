package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.device.Position;
import com.genymobile.scrcpy.util.Binary;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;

public class ControlMessageReader {

    private final DataInputStream dis;

    public ControlMessageReader(InputStream rawInputStream) {
        dis = new DataInputStream(new BufferedInputStream(rawInputStream));
    }

    public ControlMessage read() throws IOException {
        int type = dis.readUnsignedByte();
        switch (type) {
            case ControlMessage.TYPE_INJECT_KEYCODE:
                return parseInjectKeycode();
            case ControlMessage.TYPE_INJECT_TOUCH_EVENT:
                return parseInjectTouchEvent();
            case ControlMessage.TYPE_BACK_OR_SCREEN_ON:
                return parseBackOrScreenOnEvent();
            case ControlMessage.TYPE_FAST_TOUCH:
                return parseFastTouch();
            case ControlMessage.TYPE_FAST_KEY:
                return parseFastKey();
            case ControlMessage.TYPE_FAST_BATCH:
                return parseFastBatch();
            default:
                throw new ControlProtocolException("Unknown event type: " + type);
        }
    }

    private ControlMessage parseInjectKeycode() throws IOException {
        int action = dis.readUnsignedByte();
        int keycode = dis.readInt();
        int repeat = dis.readInt();
        int metaState = dis.readInt();
        return ControlMessage.createInjectKeycode(action, keycode, repeat, metaState);
    }

    private ControlMessage parseInjectTouchEvent() throws IOException {
        int action = dis.readUnsignedByte();
        long pointerId = dis.readLong();
        Position position = parsePosition();
        float pressure = Binary.u16FixedPointToFloat(dis.readShort());
        int actionButton = dis.readInt();
        int buttons = dis.readInt();
        return ControlMessage.createInjectTouchEvent(action, pointerId, position, pressure, actionButton, buttons);
    }

    private ControlMessage parseBackOrScreenOnEvent() throws IOException {
        int action = dis.readUnsignedByte();
        return ControlMessage.createBackOrScreenOn(action);
    }

    private ControlMessage parseFastTouch() throws IOException {
        int seqId = dis.readInt();
        int action = dis.readUnsignedByte();
        int x = dis.readUnsignedShort();
        int y = dis.readUnsignedShort();
        return ControlMessage.createFastTouch(seqId, action, x, y);
    }

    private ControlMessage parseFastKey() throws IOException {
        int action = dis.readUnsignedByte();
        int keycode = dis.readUnsignedShort();
        return ControlMessage.createFastKey(action, keycode);
    }

    private ControlMessage parseFastBatch() throws IOException {
        int count = dis.readUnsignedByte();
        byte[] data = new byte[count * 9];
        dis.readFully(data);
        return ControlMessage.createFastBatch(count, data);
    }

    private Position parsePosition() throws IOException {
        int x = dis.readInt();
        int y = dis.readInt();
        int screenWidth = dis.readUnsignedShort();
        int screenHeight = dis.readUnsignedShort();
        return new Position(x, y, screenWidth, screenHeight);
    }
}
