/*
 * Controller.java - 设备控制器
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 基于 Genymobile/scrcpy 二次开发
 */

package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.AndroidVersions;
import com.genymobile.scrcpy.AsyncProcessor;
import com.genymobile.scrcpy.CleanUp;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.device.Device;
import com.genymobile.scrcpy.device.Point;
import com.genymobile.scrcpy.device.Position;
import com.genymobile.scrcpy.device.Size;
import com.genymobile.scrcpy.util.IO;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.video.VirtualDisplayListener;
import com.genymobile.scrcpy.wrappers.InputManager;

import android.os.Build;
import android.os.SystemClock;
import android.util.Pair;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.io.IOException;
import java.util.concurrent.atomic.AtomicReference;

/**
 * 设备控制器 - 处理输入事件注入
 * <p>
 * 主要功能:
 * <ul>
 * <li>按键注入 (常规按键、快速按键)</li>
 * <li>触摸注入 (单点、多点、快速触摸)</li>
 * <li>返回键/亮屏处理</li>
 * </ul>
 */
public class Controller implements AsyncProcessor, VirtualDisplayListener {

    /*
     * For event injection, there are two display ids:
     * - the displayId passed to the constructor (which comes from --display-id
     * passed by the client, 0 for the main display);
     * - the virtualDisplayId used for mirroring, notified by the capture instance
     * via the VirtualDisplayListener interface.
     *
     * In order to make events work correctly in all cases:
     * - virtualDisplayId must be used for events relative to the display;
     * - displayId must be used for other events (like key events).
     */

    private static final class DisplayData {
        private final int virtualDisplayId;
        private final PositionMapper positionMapper;

        private DisplayData(int virtualDisplayId, PositionMapper positionMapper) {
            this.virtualDisplayId = virtualDisplayId;
            this.positionMapper = positionMapper;
        }
    }

    private static final int DEFAULT_DEVICE_ID = 0;
    private static final int POINTER_ID_MOUSE = -1;

    private Thread thread;

    private final int displayId;
    private final boolean supportsInputEvents;
    private final IControlChannel controlChannel;
    private final CleanUp cleanUp;
    private final DeviceMessageSender sender;
    private final boolean powerOn;

    private final AtomicReference<DisplayData> displayData = new AtomicReference<>();

    private long lastTouchDown;
    private final PointersState pointersState = new PointersState();
    private final MotionEvent.PointerProperties[] pointerProperties = new MotionEvent.PointerProperties[PointersState.MAX_POINTERS];
    private final MotionEvent.PointerCoords[] pointerCoords = new MotionEvent.PointerCoords[PointersState.MAX_POINTERS];

    // 新增：快速触摸处理器
    private final FastTouch fastTouch = new FastTouch();

    public Controller(IControlChannel controlChannel, CleanUp cleanUp, Options options) {
        this.displayId = options.getDisplayId();
        this.controlChannel = controlChannel;
        this.cleanUp = cleanUp;
        this.powerOn = options.getPowerOn();
        initPointers();
        sender = new DeviceMessageSender(controlChannel);

        supportsInputEvents = Device.supportsInputEvents(displayId);
        if (!supportsInputEvents) {
            Ln.w("Input events are not supported for secondary displays before Android 10");
        }
    }

    @Override
    public void onNewVirtualDisplay(int virtualDisplayId, PositionMapper positionMapper) {
        DisplayData data = new DisplayData(virtualDisplayId, positionMapper);
        this.displayData.set(data);

        // 更新快速触摸处理器的显示 ID 和尺寸
        fastTouch.setTargetDisplayId(virtualDisplayId);
        if (positionMapper != null && positionMapper.getDeviceSize() != null) {
            Size deviceSize = positionMapper.getDeviceSize();
            fastTouch.setDisplaySize(deviceSize.getWidth(), deviceSize.getHeight());
        }
    }

    /**
     * 设置显示尺寸（供快速触摸使用）
     */
    public void setDisplaySize(int width, int height) {
        fastTouch.setDisplaySize(width, height);
    }

    private void initPointers() {
        for (int i = 0; i < PointersState.MAX_POINTERS; ++i) {
            MotionEvent.PointerProperties props = new MotionEvent.PointerProperties();
            props.toolType = MotionEvent.TOOL_TYPE_FINGER;

            MotionEvent.PointerCoords coords = new MotionEvent.PointerCoords();
            coords.orientation = 0;
            coords.size = 0;

            pointerProperties[i] = props;
            pointerCoords[i] = coords;
        }
    }

    private void control() throws IOException {
        // on start, power on the device
        if (powerOn && displayId == 0 && !Device.isScreenOn(displayId)) {
            Device.pressReleaseKeycode(KeyEvent.KEYCODE_POWER, displayId, Device.INJECT_MODE_ASYNC);

            // dirty hack
            // After POWER is injected, the device is powered on asynchronously.
            // To turn the device screen off while mirroring, the client will send a message
            // that
            // would be handled before the device is actually powered on, so its effect
            // would
            // be "canceled" once the device is turned back on.
            // Adding this delay prevents to handle the message before the device is
            // actually
            // powered on.
            SystemClock.sleep(500);
        }

        boolean alive = true;
        while (!Thread.currentThread().isInterrupted() && alive) {
            alive = handleEvent();
        }
    }

    @Override
    public void start(TerminationListener listener) {
        // Start FastTouch worker thread for async event processing
        fastTouch.start();

        thread = new Thread(() -> {
            boolean fatalError = false;
            try {
                control();
            } catch (IOException e) {
                // Broken pipe is expected on close
                if (!IO.isBrokenPipe(e)) {
                    Ln.e("Controller error", e);
                    fatalError = true;
                }
            } finally {
                fastTouch.stop();
                listener.onTerminated(fatalError);
            }
        }, "control-recv");
        // P-KCP: 控制线程设最高优先级，减少调度延迟
        thread.setPriority(Thread.MAX_PRIORITY);
        thread.start();
        sender.start();
    }

    @Override
    public void stop() {
        if (thread != null) {
            thread.interrupt();
        }
        fastTouch.stop();
        sender.stop();
    }

    @Override
    public void join() throws InterruptedException {
        if (thread != null) {
            thread.join();
        }
        sender.join();
    }

    private boolean handleEvent() throws IOException {
        ControlMessage msg;
        try {
            msg = controlChannel.recv();
        } catch (IOException e) {
            // this is expected on close
            Ln.e("Controller recv error: " + e.getMessage());
            return false;
        }

        switch (msg.getType()) {
            case ControlMessage.TYPE_INJECT_KEYCODE:
                if (supportsInputEvents) {
                    injectKeycode(msg.getAction(), msg.getKeycode(), msg.getRepeat(), msg.getMetaState());
                }
                break;
            case ControlMessage.TYPE_INJECT_TOUCH_EVENT:
                if (supportsInputEvents) {
                    Position pos = msg.getPosition();
                    int targetDisplayId = getActionDisplayId();
                    injectTouch(msg.getAction(), msg.getPointerId(), msg.getPosition(),
                            msg.getPressure(), msg.getActionButton(), msg.getButtons());
                } else {
                    Ln.w("Input events not supported!");
                }
                break;
            case ControlMessage.TYPE_BACK_OR_SCREEN_ON:
                if (supportsInputEvents) {
                    pressBackOrTurnScreenOn(msg.getAction());
                }
                break;
            // ========== 快速消息处理 ==========
            case ControlMessage.TYPE_FAST_TOUCH:
                if (supportsInputEvents) {
                    int touchDisplayId = getActionDisplayId();
                    fastTouch.inject(msg.getSeqId(), msg.getAction(), msg.getTouchX(), msg.getTouchY(),
                            touchDisplayId);
                }
                break;
            case ControlMessage.TYPE_FAST_KEY:
                if (supportsInputEvents) {
                    injectFastKey(msg.getAction(), msg.getKeycode());
                }
                break;
            case ControlMessage.TYPE_FAST_BATCH:
                if (supportsInputEvents) {
                    fastTouch.injectBatch(msg.getData(), msg.getBatchCount());
                }
                break;
            case ControlMessage.TYPE_DISCONNECT:
                // Client requested disconnect, exit gracefully
                Ln.i("Received disconnect message from client, stopping server");
                return false;
            default:
                // do nothing
        }

        return true;
    }

    private boolean injectKeycode(int action, int keycode, int repeat, int metaState) {
        return injectKeyEvent(action, keycode, repeat, metaState, Device.INJECT_MODE_ASYNC);
    }

    /**
     * 快速按键注入 - 极简版本，无 repeat/metaState
     */
    private boolean injectFastKey(int action, int keycode) {
        int actionDisplayId = getActionDisplayId();
        long now = SystemClock.uptimeMillis();
        KeyEvent event = new KeyEvent(now, now, action, keycode, 0, 0,
                KeyCharacterMap.VIRTUAL_KEYBOARD, 0, 0, InputDevice.SOURCE_KEYBOARD);
        return Device.injectEvent(event, actionDisplayId, Device.INJECT_MODE_ASYNC);
    }

    private Pair<Point, Integer> getEventPointAndDisplayId(Position position) {
        // it hides the field on purpose, to read it with atomic access
        @SuppressWarnings("checkstyle:HiddenField")
        DisplayData displayData = this.displayData.get();
        // In scrcpy, displayData should never be null (a touch event can only be
        // generated from the client when a video frame is present).
        // However, it is possible to send events without video playback when using
        // scrcpy-server alone (except for virtual displays).
        assert displayData != null || displayId != Device.DISPLAY_ID_NONE
                : "Cannot receive a positional event without a display";

        Point point;
        int targetDisplayId;
        // 直接使用原始坐标（和原项目保持一致）
        point = position.getPoint();
        targetDisplayId = displayId;

        return Pair.create(point, targetDisplayId);
    }

    private boolean injectTouch(int action, long pointerId, Position position, float pressure, int actionButton,
            int buttons) {
        long now = SystemClock.uptimeMillis();

        Pair<Point, Integer> pair = getEventPointAndDisplayId(position);
        if (pair == null) {
            return false;
        }

        Point point = pair.first;
        int targetDisplayId = pair.second;

        int pointerIndex = pointersState.getPointerIndex(pointerId);
        if (pointerIndex == -1) {
            Ln.w("Too many pointers for touch event");
            return false;
        }
        Pointer pointer = pointersState.get(pointerIndex);
        pointer.setPoint(point);
        pointer.setPressure(pressure);

        int source;
        boolean activeSecondaryButtons = ((actionButton | buttons) & ~MotionEvent.BUTTON_PRIMARY) != 0;
        if (pointerId == POINTER_ID_MOUSE && (action == MotionEvent.ACTION_HOVER_MOVE || activeSecondaryButtons)) {
            // real mouse event, or event incompatible with a finger
            pointerProperties[pointerIndex].toolType = MotionEvent.TOOL_TYPE_MOUSE;
            source = InputDevice.SOURCE_MOUSE;
            pointer.setUp(buttons == 0);
        } else {
            // POINTER_ID_GENERIC_FINGER, POINTER_ID_VIRTUAL_FINGER or real touch from
            // device
            pointerProperties[pointerIndex].toolType = MotionEvent.TOOL_TYPE_FINGER;
            source = InputDevice.SOURCE_TOUCHSCREEN;
            // Buttons must not be set for touch events
            buttons = 0;
            pointer.setUp(action == MotionEvent.ACTION_UP);
        }

        int pointerCount = pointersState.update(pointerProperties, pointerCoords);
        if (pointerCount == 1) {
            if (action == MotionEvent.ACTION_DOWN) {
                lastTouchDown = now;
            }
        } else {
            // secondary pointers must use ACTION_POINTER_* ORed with the pointerIndex
            if (action == MotionEvent.ACTION_UP) {
                action = MotionEvent.ACTION_POINTER_UP | (pointerIndex << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
            } else if (action == MotionEvent.ACTION_DOWN) {
                action = MotionEvent.ACTION_POINTER_DOWN | (pointerIndex << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
            }
        }

        /*
         * If the input device is a mouse (on API >= 23):
         * - the first button pressed must first generate ACTION_DOWN;
         * - all button pressed (including the first one) must generate
         * ACTION_BUTTON_PRESS;
         * - all button released (including the last one) must generate
         * ACTION_BUTTON_RELEASE;
         * - the last button released must in addition generate ACTION_UP.
         *
         * Otherwise, Chrome does not work properly:
         * <https://github.com/Genymobile/scrcpy/issues/3635>
         */
        if (Build.VERSION.SDK_INT >= AndroidVersions.API_23_ANDROID_6_0 && source == InputDevice.SOURCE_MOUSE) {
            if (action == MotionEvent.ACTION_DOWN) {
                if (actionButton == buttons) {
                    // First button pressed: ACTION_DOWN
                    MotionEvent downEvent = MotionEvent.obtain(lastTouchDown, now, MotionEvent.ACTION_DOWN,
                            pointerCount, pointerProperties,
                            pointerCoords, 0, buttons, 1f, 1f, DEFAULT_DEVICE_ID, 0, source, 0);
                    if (!Device.injectEvent(downEvent, targetDisplayId, Device.INJECT_MODE_ASYNC)) {
                        downEvent.recycle();
                        return false;
                    }
                    downEvent.recycle();
                }

                // Any button pressed: ACTION_BUTTON_PRESS
                MotionEvent pressEvent = MotionEvent.obtain(lastTouchDown, now, MotionEvent.ACTION_BUTTON_PRESS,
                        pointerCount, pointerProperties,
                        pointerCoords, 0, buttons, 1f, 1f, DEFAULT_DEVICE_ID, 0, source, 0);
                if (!InputManager.setActionButton(pressEvent, actionButton)) {
                    pressEvent.recycle();
                    return false;
                }
                if (!Device.injectEvent(pressEvent, targetDisplayId, Device.INJECT_MODE_ASYNC)) {
                    pressEvent.recycle();
                    return false;
                }
                pressEvent.recycle();

                return true;
            }

            if (action == MotionEvent.ACTION_UP) {
                // Any button released: ACTION_BUTTON_RELEASE
                MotionEvent releaseEvent = MotionEvent.obtain(lastTouchDown, now, MotionEvent.ACTION_BUTTON_RELEASE,
                        pointerCount, pointerProperties,
                        pointerCoords, 0, buttons, 1f, 1f, DEFAULT_DEVICE_ID, 0, source, 0);
                if (!InputManager.setActionButton(releaseEvent, actionButton)) {
                    releaseEvent.recycle();
                    return false;
                }
                if (!Device.injectEvent(releaseEvent, targetDisplayId, Device.INJECT_MODE_ASYNC)) {
                    releaseEvent.recycle();
                    return false;
                }
                releaseEvent.recycle();

                if (buttons == 0) {
                    // Last button released: ACTION_UP
                    MotionEvent upEvent = MotionEvent.obtain(lastTouchDown, now, MotionEvent.ACTION_UP, pointerCount,
                            pointerProperties,
                            pointerCoords, 0, buttons, 1f, 1f, DEFAULT_DEVICE_ID, 0, source, 0);
                    if (!Device.injectEvent(upEvent, targetDisplayId, Device.INJECT_MODE_ASYNC)) {
                        upEvent.recycle();
                        return false;
                    }
                    upEvent.recycle();
                }

                return true;
            }
        }

        MotionEvent event = MotionEvent.obtain(lastTouchDown, now, action, pointerCount, pointerProperties,
                pointerCoords, 0, buttons, 1f, 1f,
                DEFAULT_DEVICE_ID, 0, source, 0);
        boolean result = Device.injectEvent(event, targetDisplayId, Device.INJECT_MODE_ASYNC);
        event.recycle();
        return result;
    }

    private boolean pressBackOrTurnScreenOn(int action) {
        if (displayId == Device.DISPLAY_ID_NONE || Device.isScreenOn(displayId)) {
            return injectKeyEvent(action, KeyEvent.KEYCODE_BACK, 0, 0, Device.INJECT_MODE_ASYNC);
        }

        // Screen is off - turn on
        if (action != KeyEvent.ACTION_DOWN) {
            return true;
        }
        return pressReleaseKeycode(KeyEvent.KEYCODE_POWER, Device.INJECT_MODE_ASYNC);
    }

    private boolean injectKeyEvent(int action, int keyCode, int repeat, int metaState, int injectMode) {
        return Device.injectKeyEvent(action, keyCode, repeat, metaState, getActionDisplayId(), injectMode);
    }

    private boolean pressReleaseKeycode(int keyCode, int injectMode) {
        return Device.pressReleaseKeycode(keyCode, getActionDisplayId(), injectMode);
    }

    private int getActionDisplayId() {
        if (displayId != Device.DISPLAY_ID_NONE) {
            // Real screen mirrored, use the source display id
            return displayId;
        }

        // Virtual display created by --new-display, use the virtualDisplayId
        DisplayData data = displayData.get();
        if (data == null) {
            // If no virtual display id is initialized yet, use the main display id
            return 0;
        }

        return data.virtualDisplayId;
    }
}
