package com.gamescrcpy.server.wrappers;

import android.os.IInterface;
import java.lang.reflect.Method;

public final class RotationLock {

    private static final String CALLER = "gamescrcpy";

    private final IInterface manager;
    private final Method freeze;
    private final int freezeArguments;
    private final Method thaw;
    private final int thawArguments;
    private final Method frozen;
    private final boolean frozenPerDisplay;

    private RotationLock(IInterface manager, Method freeze, int freezeArguments, Method thaw, int thawArguments,
                         Method frozen, boolean frozenPerDisplay) {
        this.manager = manager;
        this.freeze = freeze;
        this.freezeArguments = freezeArguments;
        this.thaw = thaw;
        this.thawArguments = thawArguments;
        this.frozen = frozen;
        this.frozenPerDisplay = frozenPerDisplay;
    }

    public static RotationLock create() throws PlatformException {
        final IInterface manager = SystemServices.get("window", "android.view.IWindowManager");
        final Class<?> type = manager.getClass();
        try {
            Method freeze = find(type, "freezeDisplayRotation", int.class, int.class, String.class);
            int freezeArguments = 3;
            if (freeze == null) {
                freeze = find(type, "freezeDisplayRotation", int.class, int.class);
                freezeArguments = 2;
            }
            if (freeze == null) {
                freeze = type.getMethod("freezeRotation", int.class);
                freezeArguments = 1;
            }
            Method thaw = find(type, "thawDisplayRotation", int.class, String.class);
            int thawArguments = 2;
            if (thaw == null) {
                thaw = find(type, "thawDisplayRotation", int.class);
                thawArguments = 1;
            }
            if (thaw == null) {
                thaw = type.getMethod("thawRotation");
                thawArguments = 0;
            }
            Method frozen = find(type, "isDisplayRotationFrozen", int.class);
            final boolean frozenPerDisplay = frozen != null;
            if (frozen == null) {
                frozen = type.getMethod("isRotationFrozen");
            }
            return new RotationLock(manager, freeze, freezeArguments, thaw, thawArguments, frozen, frozenPerDisplay);
        } catch (NoSuchMethodException e) {
            throw new PlatformException("the rotation cannot be locked here", e);
        }
    }

    public void freeze(int displayId, int rotation) throws PlatformException {
        try {
            switch (freezeArguments) {
                case 3 -> freeze.invoke(manager, displayId, rotation, CALLER);
                case 2 -> freeze.invoke(manager, displayId, rotation);
                default -> {
                    onlyMain(displayId);
                    freeze.invoke(manager, rotation);
                }
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot lock the rotation of display " + displayId, e);
        }
    }

    public void thaw(int displayId) throws PlatformException {
        try {
            switch (thawArguments) {
                case 2 -> thaw.invoke(manager, displayId, CALLER);
                case 1 -> thaw.invoke(manager, displayId);
                default -> {
                    onlyMain(displayId);
                    thaw.invoke(manager);
                }
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot unlock the rotation of display " + displayId, e);
        }
    }

    public boolean isFrozen(int displayId) throws PlatformException {
        try {
            if (frozenPerDisplay) {
                return (boolean) frozen.invoke(manager, displayId);
            }
            onlyMain(displayId);
            return (boolean) frozen.invoke(manager);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot read the rotation lock of display " + displayId, e);
        }
    }

    private static void onlyMain(int displayId) throws PlatformException {
        if (displayId != 0) {
            throw new PlatformException("only the main display's rotation can be locked before Android 10");
        }
    }

    private static Method find(Class<?> type, String name, Class<?>... parameters) {
        try {
            return type.getMethod(name, parameters);
        } catch (NoSuchMethodException e) {
            return null;
        }
    }
}
