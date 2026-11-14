package com.gamescrcpy.server.wrappers;

import android.os.Process;
import android.view.DisplayCutout;
import com.gamescrcpy.server.device.DisplaySnapshot;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

public final class Displays {

    private static final int TYPE_VIRTUAL = 5;

    private static Displays instance;

    private final Object global;
    private final Method getDisplayIds;
    private final Method getDisplayInfo;
    private final Fields fields;

    private Displays(Object global) throws ReflectiveOperationException {
        this.global = global;
        this.getDisplayIds = global.getClass().getMethod("getDisplayIds");
        this.getDisplayInfo = global.getClass().getMethod("getDisplayInfo", int.class);
        this.fields = new Fields(Class.forName("android.view.DisplayInfo"));
    }

    public static synchronized Displays get() throws PlatformException {
        if (instance == null) {
            try {
                final Class<?> globalClass = Class.forName("android.hardware.display.DisplayManagerGlobal");
                instance = new Displays(globalClass.getDeclaredMethod("getInstance").invoke(null));
            } catch (ReflectiveOperationException | RuntimeException e) {
                throw new PlatformException("DisplayManagerGlobal is unavailable", e);
            }
        }
        return instance;
    }

    public List<DisplaySnapshot> list() throws PlatformException {
        final List<DisplaySnapshot> displays = new ArrayList<>();
        final int[] ids;
        try {
            ids = (int[]) getDisplayIds.invoke(global);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot list displays", e);
        }
        for (int id : ids) {
            final Object info = rawInfo(id);
            if (info != null && !ownVirtualDisplay(info)) {
                displays.add(snapshot(id, info));
            }
        }
        return displays;
    }

    public DisplaySnapshot snapshot(int displayId) throws PlatformException {
        final Object info = rawInfo(displayId);
        return info == null || ownVirtualDisplay(info) ? null : snapshot(displayId, info);
    }

    public DisplaySnapshot snapshotOfAny(int displayId) throws PlatformException {
        final Object info = rawInfo(displayId);
        return info == null ? null : snapshot(displayId, info);
    }

    private Object rawInfo(int displayId) throws PlatformException {
        try {
            return getDisplayInfo.invoke(global, displayId);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot read display " + displayId, e);
        }
    }

    private boolean ownVirtualDisplay(Object info) throws PlatformException {
        try {
            return fields.type.getInt(info) == TYPE_VIRTUAL && fields.ownerUid.getInt(info) == Process.myUid();
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("unexpected DisplayInfo layout", e);
        }
    }

    private DisplaySnapshot snapshot(int displayId, Object info) throws PlatformException {
        try {
            final DisplayCutout cutout = (DisplayCutout) fields.displayCutout.get(info);
            return new DisplaySnapshot(displayId, fields.logicalWidth.getInt(info), fields.logicalHeight.getInt(info),
                    fields.rotation.getInt(info), fields.layerStack.getInt(info), fields.flags.getInt(info),
                    fields.logicalDensityDpi.getInt(info), refreshMilliHz(info), fields.state.getInt(info),
                    cutout == null ? 0 : cutout.getSafeInsetLeft(), cutout == null ? 0 : cutout.getSafeInsetTop(),
                    cutout == null ? 0 : cutout.getSafeInsetRight(), cutout == null ? 0 : cutout.getSafeInsetBottom());
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("unexpected DisplayInfo layout", e);
        }
    }

    private long refreshMilliHz(Object info) {
        try {
            final float hz = (float) fields.info.getMethod("getRefreshRate").invoke(info);
            return Math.round(hz * 1000.0);
        } catch (ReflectiveOperationException | RuntimeException e) {
            try {
                final Object mode = fields.info.getMethod("getMode").invoke(info);
                return Math.round(((android.view.Display.Mode) mode).getRefreshRate() * 1000.0);
            } catch (ReflectiveOperationException | RuntimeException e2) {
                return 60_000;
            }
        }
    }

    private static final class Fields {
        final Class<?> info;
        final Field logicalWidth;
        final Field logicalHeight;
        final Field rotation;
        final Field layerStack;
        final Field flags;
        final Field logicalDensityDpi;
        final Field state;
        final Field type;
        final Field ownerUid;
        final Field displayCutout;

        Fields(Class<?> info) throws NoSuchFieldException {
            this.info = info;
            logicalWidth = info.getDeclaredField("logicalWidth");
            logicalHeight = info.getDeclaredField("logicalHeight");
            rotation = info.getDeclaredField("rotation");
            layerStack = info.getDeclaredField("layerStack");
            flags = info.getDeclaredField("flags");
            logicalDensityDpi = info.getDeclaredField("logicalDensityDpi");
            state = info.getDeclaredField("state");
            type = info.getDeclaredField("type");
            ownerUid = info.getDeclaredField("ownerUid");
            displayCutout = info.getDeclaredField("displayCutout");
        }
    }
}
