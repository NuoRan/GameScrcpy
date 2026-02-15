package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.device.Position;

/**
 * 游戏投屏控制消息 - 极简版本
 * 只保留按键、触摸和返回键功能
 */
public final class ControlMessage {

    // 核心消息类型（保持原值以兼容原版 scrcpy 客户端）
    public static final int TYPE_INJECT_KEYCODE = 0;
    public static final int TYPE_INJECT_TOUCH_EVENT = 2;
    public static final int TYPE_BACK_OR_SCREEN_ON = 4;

    // 极简协议 v2: action 编码在 type 中，seqId 压缩到 1 字节
    public static final int TYPE_TOUCH_DOWN  = 10;  // 6B: seqId(1)+x(2)+y(2)
    public static final int TYPE_TOUCH_UP    = 11;  // 6B
    public static final int TYPE_TOUCH_MOVE  = 12;  // 6B
    public static final int TYPE_TOUCH_RESET = 13;  // 1B: 无载荷
    public static final int TYPE_KEY_DOWN    = 14;  // 3B: keycode(2)
    public static final int TYPE_KEY_UP      = 15;  // 3B
    public static final int TYPE_BATCH       = 16;  // 2+6N: count(1)+[seqId(1)+action(1)+x(2)+y(2)]*N
    public static final int TYPE_DISCONNECT  = 0xFF; // 1B

    // 核心字段
    private int type;
    private int metaState;     // KeyEvent.META_*
    private int action;        // KeyEvent.ACTION_* or MotionEvent.ACTION_*
    private int keycode;       // KeyEvent.KEYCODE_*
    private int actionButton;  // MotionEvent.BUTTON_*
    private int buttons;       // MotionEvent.BUTTON_*
    private long pointerId;
    private float pressure;
    private Position position;
    private int repeat;
    private byte[] data;

    // 快速触摸字段
    private int seqId;
    private int touchX;
    private int touchY;
    private int batchCount;

    private ControlMessage() {
    }

    // FastTouch 对象池 (单线程，避免高频触摸 GC)
    private static final ControlMessage REUSABLE_FAST_TOUCH = new ControlMessage();
    private static final ControlMessage REUSABLE_FAST_KEY = new ControlMessage();

    public static ControlMessage createInjectKeycode(int action, int keycode, int repeat, int metaState) {
        ControlMessage msg = new ControlMessage();
        msg.type = TYPE_INJECT_KEYCODE;
        msg.action = action;
        msg.keycode = keycode;
        msg.repeat = repeat;
        msg.metaState = metaState;
        return msg;
    }

    public static ControlMessage createInjectTouchEvent(int action, long pointerId, Position position, float pressure,
            int actionButton, int buttons) {
        ControlMessage msg = new ControlMessage();
        msg.type = TYPE_INJECT_TOUCH_EVENT;
        msg.action = action;
        msg.pointerId = pointerId;
        msg.pressure = pressure;
        msg.position = position;
        msg.actionButton = actionButton;
        msg.buttons = buttons;
        return msg;
    }

    public static ControlMessage createBackOrScreenOn(int action) {
        ControlMessage msg = new ControlMessage();
        msg.type = TYPE_BACK_OR_SCREEN_ON;
        msg.action = action;
        return msg;
    }

    // ========== 快速消息创建方法 ==========

    public static ControlMessage createFastTouch(int type, int seqId, int action, int x, int y) {
        ControlMessage msg = REUSABLE_FAST_TOUCH;
        msg.type = type;
        msg.seqId = seqId;
        msg.action = action;
        msg.touchX = x;
        msg.touchY = y;
        return msg;
    }

    public static ControlMessage createFastKey(int type, int keycode) {
        ControlMessage msg = REUSABLE_FAST_KEY;
        msg.type = type;
        msg.keycode = keycode;
        return msg;
    }

    public static ControlMessage createFastBatch(int count, byte[] data) {
        ControlMessage msg = new ControlMessage();
        msg.type = TYPE_BATCH;
        msg.batchCount = count;
        msg.data = data;
        return msg;
    }

    public static ControlMessage createDisconnect() {
        ControlMessage msg = new ControlMessage();
        msg.type = TYPE_DISCONNECT;
        return msg;
    }

    // ========== Getters ==========

    public int getType() {
        return type;
    }

    public int getMetaState() {
        return metaState;
    }

    public int getAction() {
        return action;
    }

    public int getKeycode() {
        return keycode;
    }

    public int getActionButton() {
        return actionButton;
    }

    public int getButtons() {
        return buttons;
    }

    public long getPointerId() {
        return pointerId;
    }

    public float getPressure() {
        return pressure;
    }

    public Position getPosition() {
        return position;
    }

    public int getRepeat() {
        return repeat;
    }

    public byte[] getData() {
        return data;
    }

    public int getSeqId() {
        return seqId;
    }

    public int getTouchX() {
        return touchX;
    }

    public int getTouchY() {
        return touchY;
    }

    public int getBatchCount() {
        return batchCount;
    }
}
