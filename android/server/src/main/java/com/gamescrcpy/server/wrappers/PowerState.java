package com.gamescrcpy.server.wrappers;

import android.content.Context;
import android.os.PowerManager;

public final class PowerState {

    private PowerState() {
    }

    public static boolean interactive() {
        final Context context = ProcessEnvironment.context();
        final PowerManager power = context != null ? context.getSystemService(PowerManager.class) : null;
        return power == null || power.isInteractive();
    }
}
