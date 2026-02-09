package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.device.Position;
import com.genymobile.scrcpy.util.Binary;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;

public class ControlMessageReader {

    private final DataInputStream dis;
    // P-KCP: 预分配 batch 缓冲区，避免每次 parseFastBatch 都 new byte[]
    private byte[] batchBuffer = new byte[255 * 9]; // 最大 batch 大小

    public ControlMessageReader(InputStream rawInputStream) {
        // 最小化缓冲区：控制消息极小 (4-10 bytes)，默认 8KB 导致小消息被缓冲增加延迟
        dis = new DataInputStream(new BufferedInputStream(rawInputStream, 64));
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
            case ControlMessage.TYPE_DISCONNECT:
                return parseDisconnect();
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
        int len = count * 9;
        // P-KCP: 复用预分配缓冲区
        dis.readFully(batchBuffer, 0, len);
        return ControlMessage.createFastBatch(count, batchBuffer);
    }

    private ControlMessage parseDisconnect() {
        return ControlMessage.createDisconnect();
    }

    private Position parsePosition() throws IOException {
        int x = dis.readInt();
        int y = dis.readInt();
        int screenWidth = dis.readUnsignedShort();
        int screenHeight = dis.readUnsignedShort();
        return new Position(x, y, screenWidth, screenHeight);
    }
}
