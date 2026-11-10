package com.gamescrcpy.server.device;

import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.wrappers.Displays;
import com.gamescrcpy.server.wrappers.PlatformException;
import com.gamescrcpy.server.wrappers.RotationLock;

public final class OrientationLock {

    private static final int MAIN_DISPLAY = 0;

    private final RotationLock lock;
    private boolean locked;
    private boolean wasFrozen;
    private int wasRotation;

    public OrientationLock(RotationLock lock) {
        this.lock = lock;
    }

    public synchronized void set(int mode) throws PlatformException {
        if (mode == 0) {
            restore();
            return;
        }
        if (!locked) {
            wasFrozen = lock.isFrozen(MAIN_DISPLAY);
            final DisplaySnapshot display = Displays.get().snapshot(MAIN_DISPLAY);
            wasRotation = display != null ? display.rotation() : 0;
        }
        lock.freeze(MAIN_DISPLAY, mode - 1);
        locked = true;
        Log.i("orientation locked at rotation " + (mode - 1));
    }

    public synchronized void restore() {
        if (!locked) {
            return;
        }
        locked = false;
        try {
            if (wasFrozen) {
                lock.freeze(MAIN_DISPLAY, wasRotation);
                Log.i("orientation given back: locked at rotation " + wasRotation);
            } else {
                lock.thaw(MAIN_DISPLAY);
                Log.i("orientation given back: following the device");
            }
        } catch (PlatformException e) {
            Log.w("cannot give the device its rotation back", e);
        }
    }
}
