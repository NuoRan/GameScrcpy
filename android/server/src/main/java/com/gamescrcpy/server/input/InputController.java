package com.gamescrcpy.server.input;

import android.os.Build;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import com.gamescrcpy.protocol.Enums.KeyAction;
import com.gamescrcpy.protocol.Messages.BackOrScreenOn;
import com.gamescrcpy.protocol.Messages.Key;
import com.gamescrcpy.protocol.Messages.Scroll;
import com.gamescrcpy.protocol.Messages.Text;
import com.gamescrcpy.protocol.Messages.TouchFrame;
import com.gamescrcpy.server.wrappers.InputInjector;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BooleanSupplier;
import java.util.function.Supplier;

public final class InputController {

    private static final int FLAG_CANCELED = 0x20;
    private static final int POINTERS = 32;

    private final InputInjector injector;
    private final TextInjector text;
    private final Supplier<TouchGeometry> geometry;
    private final BooleanSupplier screenOn;
    private final TouchPlanner planner = new TouchPlanner();
    private final MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[POINTERS];
    private final MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[POINTERS];
    private final MotionEvent.PointerProperties[] scrollProperties = {new MotionEvent.PointerProperties()};
    private final MotionEvent.PointerCoords[] scrollCoords = {new MotionEvent.PointerCoords()};
    private final Map<Integer, Long> pressedKeys = new HashMap<>();
    private long gestureDownTime;
    private long backDownTime = -1;

    public InputController(InputInjector injector, TextInjector text, Supplier<TouchGeometry> geometry,
                           BooleanSupplier screenOn) {
        this.injector = injector;
        this.text = text;
        this.geometry = geometry;
        this.screenOn = screenOn;
        for (int i = 0; i < POINTERS; ++i) {
            properties[i] = new MotionEvent.PointerProperties();
            properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coords[i] = new MotionEvent.PointerCoords();
        }
        scrollProperties[0].id = 0;
        scrollProperties[0].toolType = MotionEvent.TOOL_TYPE_MOUSE;
    }

    public void touch(TouchFrame frame) {
        final int count = planner.plan(frame, geometry.get());
        for (int i = 0; i < count; ++i) {
            injectMotion(planner.event(i));
        }
    }

    public void key(Key message) {
        final int keyCode = (int) message.keycode;
        final long now = SystemClock.uptimeMillis();
        final Long pressed;
        final int action;
        if (message.action == KeyAction.DOWN) {
            pressed = pressedKeys.get(keyCode);
            pressedKeys.put(keyCode, pressed != null ? pressed : now);
            action = KeyEvent.ACTION_DOWN;
        } else {
            pressed = pressedKeys.remove(keyCode);
            action = KeyEvent.ACTION_UP;
        }
        injectKey(pressed != null ? pressed : now, now, action, keyCode, message.repeat, (int) message.metaState, 0);
    }

    public void text(Text message) {
        text.type(message.text, keyDisplay());
    }

    public void scroll(Scroll message) {
        final TouchGeometry current = geometry.get();
        if (current == null || message.videoSession != current.session()) {
            return;
        }
        final MotionEvent.PointerCoords point = scrollCoords[0];
        point.clear();
        point.x = current.displayX(message.x);
        point.y = current.displayY(message.y);
        point.setAxisValue(MotionEvent.AXIS_HSCROLL, message.hScroll / 256f);
        point.setAxisValue(MotionEvent.AXIS_VSCROLL, message.vScroll / 256f);
        final long now = SystemClock.uptimeMillis();
        final MotionEvent event = MotionEvent.obtain(now, now, MotionEvent.ACTION_SCROLL, 1, scrollProperties,
                scrollCoords, 0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_MOUSE, 0);
        injector.inject(event, current.displayId(), InputInjector.MODE_ASYNC);
        event.recycle();
    }

    public void backOrScreenOn(BackOrScreenOn message) {
        final long now = SystemClock.uptimeMillis();
        if (message.action == KeyAction.DOWN) {
            if (screenOn.getAsBoolean()) {
                backDownTime = now;
                injectKey(now, now, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_BACK, 0, 0, 0);
            } else {
                injectKey(now, now, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_WAKEUP, 0, 0, 0);
                injectKey(now, now, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_WAKEUP, 0, 0, 0);
            }
        } else if (backDownTime >= 0) {
            injectKey(backDownTime, now, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK, 0, 0, 0);
            backDownTime = -1;
        }
    }

    public void releaseAll() {
        final int count = planner.cancelAll();
        for (int i = 0; i < count; ++i) {
            injectMotion(planner.event(i));
        }
        final long now = SystemClock.uptimeMillis();
        for (Map.Entry<Integer, Long> key : pressedKeys.entrySet()) {
            injectKey(key.getValue(), now, KeyEvent.ACTION_UP, key.getKey(), 0, 0, FLAG_CANCELED);
        }
        pressedKeys.clear();
        if (backDownTime >= 0) {
            injectKey(backDownTime, now, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_BACK, 0, 0, FLAG_CANCELED);
            backDownTime = -1;
        }
    }

    private void injectMotion(TouchPlanner.Event planned) {
        final long now = SystemClock.uptimeMillis();
        if (planned.gestureStart) {
            gestureDownTime = now;
        }
        for (int i = 0; i < planned.pointerCount; ++i) {
            properties[i].id = planned.ids[i];
            coords[i].x = planned.xs[i];
            coords[i].y = planned.ys[i];
            coords[i].pressure = planned.pressures[i];
        }
        final int flags = planned.canceled && Build.VERSION.SDK_INT >= 33 ? FLAG_CANCELED : 0;
        final MotionEvent event = MotionEvent.obtain(gestureDownTime, now, planned.action, planned.pointerCount,
                properties, coords, 0, 0, 1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, flags);
        injector.inject(event, planned.displayId, InputInjector.MODE_ASYNC);
        event.recycle();
    }

    private void injectKey(long downTime, long eventTime, int action, int keyCode, int repeat, int metaState,
                           int flags) {
        final KeyEvent event = new KeyEvent(downTime, eventTime, action, keyCode, repeat, metaState,
                KeyCharacterMap.VIRTUAL_KEYBOARD, 0, flags, InputDevice.SOURCE_KEYBOARD);
        final int displayId = keyDisplay();
        if (text.busy()) {
            text.submit(() -> injector.inject(event, displayId, InputInjector.MODE_ASYNC));
        } else {
            injector.inject(event, displayId, InputInjector.MODE_ASYNC);
        }
    }

    int keyDisplay() {
        final TouchGeometry current = geometry.get();
        return current != null ? current.displayId() : 0;
    }
}
