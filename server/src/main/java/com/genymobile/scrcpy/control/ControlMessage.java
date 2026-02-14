package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.device.Position;

/**
 * 游戏投屏控制消息 - 极简版本
 * 只保留按键、触摸和返回键功能
 */
public final class ControlMessage {

    // 核心消息类型（保持原值以兼容客户端）
    public static final int TYPE_INJECT_KEYCODE = 0;
    public static final int TYPE_INJECT_TOUCH_EVENT = 2;
    public static final int TYPE_BACK_OR_SCREEN_ON = 4;

    // 快速消息类型（游戏优化）
    public static final int TYPE_FAST_TOUCH = 100;
    public static final int TYPE_FAST_KEY = 101;
    public static final int TYPE_FAST_BATCH = 102;

    // 断开连接消息（客户端主动关闭时发送）
    public static final int TYPE_DISCONNECT = 200;

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

    // [极致低延迟优化] FastTouch 对象池 — 控制线程单线程使用，无需同步
    // 避免高频触摸消息每次 new ControlMessage() 的 GC 压力
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

    public static ControlMessage createFastTouch(int seqId, int action, int x, int y) {
        // [极致低延迟优化] 复用预分配对象，control-recv 单线程安全
        ControlMessage msg = REUSABLE_FAST_TOUCH;
        msg.type = TYPE_FAST_TOUCH;
        msg.seqId = seqId;
        msg.action = action;
        msg.touchX = x;
        msg.touchY = y;
        return msg;
    }

    public static ControlMessage createFastKey(int action, int keycode) {
        // [极致低延迟优化] 复用预分配对象，control-recv 单线程安全
        ControlMessage msg = REUSABLE_FAST_KEY;
        msg.type = TYPE_FAST_KEY;
        msg.action = action;
        msg.keycode = keycode;
        return msg;
    }

    public static ControlMessage createFastBatch(int count, byte[] data) {
        ControlMessage msg = new ControlMessage();
        msg.type = TYPE_FAST_BATCH;
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
