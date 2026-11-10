package com.gamescrcpy.server.device;

import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.wrappers.DisplayPower;
import com.gamescrcpy.server.wrappers.PlatformException;

public final class ScreenPower {

    private final DisplayPower power;
    private boolean turnedOff;

    public ScreenPower(DisplayPower power) {
        this.power = power;
    }

    public synchronized void set(boolean on) throws PlatformException {
        power.set(on);
        turnedOff = !on;
        Log.i(on ? "screen turned on" : "screen turned off while mirroring");
    }

    public synchronized void restore() {
        if (!turnedOff) {
            return;
        }
        turnedOff = false;
        try {
            power.set(true);
            Log.i("screen turned back on");
        } catch (PlatformException e) {
            Log.w("cannot turn the screen back on", e);
        }
    }
}
