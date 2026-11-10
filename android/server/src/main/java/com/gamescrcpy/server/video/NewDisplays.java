package com.gamescrcpy.server.video;

import android.annotation.SuppressLint;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.os.Build;
import android.view.Surface;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.wrappers.PlatformException;
import com.gamescrcpy.server.wrappers.ProcessEnvironment;
import java.lang.reflect.Constructor;
import java.util.HashMap;
import java.util.Map;

public final class NewDisplays implements IndependentDisplays {

    public static final int MINIMUM_SDK = 30;

    private static final String NAME = "GameScrcpy";
    private static final int FLAG_SUPPORTS_TOUCH = 1 << 6;
    private static final int FLAG_ROTATES_WITH_CONTENT = 1 << 7;
    private static final int FLAG_DESTROY_CONTENT_ON_REMOVAL = 1 << 8;
    private static final int FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS = 1 << 9;
    private static final int FLAG_TRUSTED = 1 << 10;
    private static final int FLAG_OWN_DISPLAY_GROUP = 1 << 11;
    private static final int FLAG_ALWAYS_UNLOCKED = 1 << 12;
    private static final int FLAG_TOUCH_FEEDBACK_DISABLED = 1 << 13;
    private static final int FLAG_OWN_FOCUS = 1 << 14;
    private static final int FLAG_DEVICE_DISPLAY_GROUP = 1 << 15;

    private final DisplayManager manager;
    private final Map<Integer, VirtualDisplay> displays = new HashMap<>();

    private NewDisplays(DisplayManager manager) {
        this.manager = manager;
    }

    public static NewDisplays create() throws PlatformException {
        if (Build.VERSION.SDK_INT < MINIMUM_SDK) {
            throw new PlatformException("independent displays need Android 11 or later");
        }
        final Context context = ProcessEnvironment.context();
        if (context == null) {
            throw new PlatformException("no shell context to create displays");
        }
        try {
            final Constructor<DisplayManager> constructor = DisplayManager.class.getDeclaredConstructor(Context.class);
            constructor.setAccessible(true);
            return new NewDisplays(constructor.newInstance(context));
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("the shell cannot create displays", e);
        }
    }

    @Override
    @SuppressLint("WrongConstant")
    public synchronized int create(int width, int height, int densityDpi) throws PlatformException {
        final VirtualDisplay display;
        try {
            display = manager.createVirtualDisplay(NAME, width, height, densityDpi, null, flags());
        } catch (RuntimeException e) {
            throw new PlatformException("cannot create a " + width + "x" + height + " display", e);
        }
        if (display == null) {
            throw new PlatformException("the system refused a " + width + "x" + height + " display");
        }
        final int displayId = display.getDisplay().getDisplayId();
        displays.put(displayId, display);
        Log.i("independent display " + displayId + ": " + width + "x" + height + " at " + densityDpi + " dpi");
        return displayId;
    }

    @Override
    public synchronized void resize(int displayId, int width, int height, int densityDpi) {
        final VirtualDisplay display = displays.get(displayId);
        if (display == null) {
            return;
        }
        try {
            display.resize(width, height, densityDpi);
            Log.i("independent display " + displayId + " turned to " + width + "x" + height + " to stay upright");
        } catch (RuntimeException e) {
            Log.w("cannot resize independent display " + displayId, e);
        }
    }

    @Override
    public synchronized void release(int displayId) {
        final VirtualDisplay display = displays.remove(displayId);
        if (display != null) {
            display.release();
            Log.i("independent display " + displayId + " released with its content");
        }
    }

    public synchronized boolean owns(int displayId) {
        return displays.containsKey(displayId);
    }

    public synchronized void attach(int displayId, Surface surface) {
        final VirtualDisplay display = displays.get(displayId);
        if (display != null) {
            display.setSurface(surface);
        }
    }

    private static int flags() {
        int flags = DisplayManager.VIRTUAL_DISPLAY_FLAG_PUBLIC | DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION
                | DisplayManager.VIRTUAL_DISPLAY_FLAG_OWN_CONTENT_ONLY | FLAG_SUPPORTS_TOUCH | FLAG_ROTATES_WITH_CONTENT
                | FLAG_DESTROY_CONTENT_ON_REMOVAL | FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS;
        if (Build.VERSION.SDK_INT >= 33) {
            flags |= FLAG_TRUSTED | FLAG_OWN_DISPLAY_GROUP | FLAG_ALWAYS_UNLOCKED | FLAG_TOUCH_FEEDBACK_DISABLED;
        }
        if (Build.VERSION.SDK_INT >= 34) {
            flags |= FLAG_OWN_FOCUS | FLAG_DEVICE_DISPLAY_GROUP;
        }
        return flags;
    }
}
