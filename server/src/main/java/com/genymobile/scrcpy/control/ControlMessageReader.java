package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.device.Position;
import com.genymobile.scrcpy.util.Binary;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.InputStream;

public class ControlMessageReader {

    private final DataInputStream dis;
    private byte[] batchBuffer = new byte[255 * 6]; // v2: 6 bytes/event

    public ControlMessageReader(InputStream rawInputStream) {
        // 最小缓冲区：控制消息极小 (1-6 bytes)
        dis = new DataInputStream(new BufferedInputStream(rawInputStream, 64));
    }

    public ControlMessage read() throws IOException {
        int type = dis.readUnsignedByte();
        switch (type) {
            // 原版 scrcpy 标准类型
            case ControlMessage.TYPE_INJECT_KEYCODE:
                return parseInjectKeycode();
            case ControlMessage.TYPE_INJECT_TOUCH_EVENT:
                return parseInjectTouchEvent();
            case ControlMessage.TYPE_BACK_OR_SCREEN_ON:
                return parseBackOrScreenOnEvent();
            // 极简协议 v2
            case ControlMessage.TYPE_TOUCH_DOWN:
            case ControlMessage.TYPE_TOUCH_UP:
            case ControlMessage.TYPE_TOUCH_MOVE:
                return parseTouchV2(type);
            case ControlMessage.TYPE_TOUCH_RESET:
                return parseTouchReset();
            case ControlMessage.TYPE_KEY_DOWN:
            case ControlMessage.TYPE_KEY_UP:
                return parseKeyV2(type);
            case ControlMessage.TYPE_BATCH:
                return parseBatchV2();
            case ControlMessage.TYPE_DISCONNECT:
                return ControlMessage.createDisconnect();
            default:
                throw new ControlProtocolException("Unknown event type: " + type);
        }
    }

    // v2: seqId(1)+x(2)+y(2) = 5 payload bytes, action 从 type 推导
    private ControlMessage parseTouchV2(int type) throws IOException {
        int seqId = dis.readUnsignedByte();
        int action = type - ControlMessage.TYPE_TOUCH_DOWN; // 10→0(DOWN), 11→1(UP), 12→2(MOVE)
        int x = dis.readUnsignedShort();
        int y = dis.readUnsignedShort();
        return ControlMessage.createFastTouch(type, seqId, action, x, y);
    }

    // v2: RESET 无载荷
    private ControlMessage parseTouchReset() {
        return ControlMessage.createFastTouch(ControlMessage.TYPE_TOUCH_RESET, 0, 3, 0, 0);
    }

    // v2: keycode(2) = 2 payload bytes, action 从 type 推导
    private ControlMessage parseKeyV2(int type) throws IOException {
        int keycode = dis.readUnsignedShort();
        return ControlMessage.createFastKey(type, keycode);
    }

    // v2: count(1) + [seqId(1)+action(1)+x(2)+y(2)]*N = 6 bytes/event
    private ControlMessage parseBatchV2() throws IOException {
        int count = dis.readUnsignedByte();
        int len = count * 6;
        dis.readFully(batchBuffer, 0, len);
        return ControlMessage.createFastBatch(count, batchBuffer);
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

    private Position parsePosition() throws IOException {
        int x = dis.readInt();
        int y = dis.readInt();
        int screenWidth = dis.readUnsignedShort();
        int screenHeight = dis.readUnsignedShort();
        return new Position(x, y, screenWidth, screenHeight);
    }
}
