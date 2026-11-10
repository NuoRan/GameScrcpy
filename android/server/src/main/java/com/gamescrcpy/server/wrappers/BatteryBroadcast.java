package com.gamescrcpy.server.wrappers;

import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.IInterface;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.device.DeviceStatusSampler;
import java.lang.reflect.Method;

public final class BatteryBroadcast implements DeviceStatusSampler.Battery {

    private static final String[] REGISTER_METHODS = {"registerReceiverWithFeature", "registerReceiver"};

    private final IInterface activities;
    private final Method register;
    private final IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
    private boolean warned;

    private BatteryBroadcast(IInterface activities, Method register) {
        this.activities = activities;
        this.register = register;
    }

    public static BatteryBroadcast create() throws PlatformException {
        final IInterface activities = SystemServices.get("activity", "android.app.IActivityManager");
        for (String name : REGISTER_METHODS) {
            final Method register = stickyLookup(activities.getClass(), name);
            if (register != null) {
                return new BatteryBroadcast(activities, register);
            }
        }
        throw new PlatformException("the activity manager hands out no sticky broadcasts");
    }

    @Override
    public synchronized DeviceStatusSampler.BatteryReading read() {
        final Intent sticky;
        try {
            sticky = (Intent) register.invoke(activities, arguments(register.getParameterTypes()));
        } catch (ReflectiveOperationException | RuntimeException e) {
            warn("the battery broadcast cannot be read", e);
            return null;
        }
        if (sticky == null) {
            warn("the phone has sent no battery broadcast", null);
            return null;
        }
        return new DeviceStatusSampler.BatteryReading(
                sticky.getBooleanExtra(BatteryManager.EXTRA_PRESENT, true),
                sticky.getIntExtra(BatteryManager.EXTRA_LEVEL, -1),
                sticky.getIntExtra(BatteryManager.EXTRA_SCALE, -1),
                sticky.getIntExtra(BatteryManager.EXTRA_STATUS, BatteryManager.BATTERY_STATUS_UNKNOWN),
                sticky.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0),
                sticky.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE));
    }

    private static Method stickyLookup(Class<?> type, String name) {
        for (Method method : type.getMethods()) {
            if (method.getName().equals(name) && method.getReturnType() == Intent.class && fits(method)) {
                return method;
            }
        }
        return null;
    }

    private static boolean fits(Method method) {
        int filters = 0;
        for (Class<?> parameter : method.getParameterTypes()) {
            if (parameter == IntentFilter.class) {
                ++filters;
            } else if (parameter.isPrimitive() && parameter != int.class) {
                return false;
            }
        }
        return filters == 1;
    }

    private Object[] arguments(Class<?>[] types) {
        final Object[] values = new Object[types.length];
        boolean named = false;
        for (int i = 0; i < types.length; ++i) {
            final Class<?> type = types[i];
            if (type == IntentFilter.class) {
                values[i] = filter;
            } else if (type == int.class) {
                values[i] = 0;
            } else if (type == String.class) {
                values[i] = named ? null : ProcessEnvironment.SHELL_PACKAGE;
                named = true;
            }
        }
        return values;
    }

    private void warn(String message, Exception cause) {
        if (warned) {
            return;
        }
        warned = true;
        if (cause == null) {
            Log.w(message + "; the battery level and temperature are not reported");
        } else {
            Log.w(message + "; the battery level and temperature are not reported", cause);
        }
    }
}
