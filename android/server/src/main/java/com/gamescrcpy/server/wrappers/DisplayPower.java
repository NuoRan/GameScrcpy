package com.gamescrcpy.server.wrappers;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.IBinder;
import android.system.Os;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

public final class DisplayPower {

    private static final int POWER_MODE_OFF = 0;
    private static final int POWER_MODE_NORMAL = 2;

    private interface Tokens {
        List<IBinder> get() throws ReflectiveOperationException;
    }

    private final Method setPowerMode;
    private final Tokens tokens;

    private DisplayPower(Method setPowerMode, Tokens tokens) {
        this.setPowerMode = setPowerMode;
        this.tokens = tokens;
    }

    public static DisplayPower create() throws PlatformException {
        try {
            final Class<?> surfaceControl = Class.forName("android.view.SurfaceControl");
            return new DisplayPower(surfaceControl.getMethod("setDisplayPowerMode", IBinder.class, int.class),
                    tokens(surfaceControl));
        } catch (ReflectiveOperationException | RuntimeException | LinkageError e) {
            throw new PlatformException("the screen cannot be turned off here", e);
        }
    }

    public void set(boolean on) throws PlatformException {
        final int mode = on ? POWER_MODE_NORMAL : POWER_MODE_OFF;
        try {
            for (IBinder token : tokens.get()) {
                setPowerMode.invoke(null, token, mode);
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot turn the screen " + (on ? "on" : "off"), e);
        }
    }

    private static Tokens tokens(Class<?> surfaceControl) throws ReflectiveOperationException {
        if (Build.VERSION.SDK_INT < 29) {
            final Method builtIn = surfaceControl.getMethod("getBuiltInDisplay", int.class);
            return () -> List.of((IBinder) builtIn.invoke(null, 0));
        }
        if (Build.VERSION.SDK_INT >= 34 && "honor".equalsIgnoreCase(Build.BRAND)) {
            try {
                final Method internal = surfaceControl.getMethod("getInternalDisplayToken");
                return () -> List.of((IBinder) internal.invoke(null));
            } catch (NoSuchMethodException e) {
            }
        }
        Method ids;
        Method token;
        try {
            ids = surfaceControl.getMethod("getPhysicalDisplayIds");
            token = surfaceControl.getMethod("getPhysicalDisplayToken", long.class);
        } catch (NoSuchMethodException e) {
            if (Build.VERSION.SDK_INT < 34) {
                throw e;
            }
            final Class<?> displayControl = displayControl();
            ids = displayControl.getMethod("getPhysicalDisplayIds");
            token = displayControl.getMethod("getPhysicalDisplayToken", long.class);
        }
        final Method idsMethod = ids;
        final Method tokenMethod = token;
        return () -> {
            final List<IBinder> result = new ArrayList<>();
            for (long id : (long[]) idsMethod.invoke(null)) {
                result.add((IBinder) tokenMethod.invoke(null, id));
            }
            return result;
        };
    }

    @SuppressLint({"PrivateApi", "DiscouragedPrivateApi", "SoonBlockedPrivateApi", "BlockedPrivateApi"})
    private static Class<?> displayControl() throws ReflectiveOperationException {
        final Method createClassLoader = Class.forName("com.android.internal.os.ClassLoaderFactory").getDeclaredMethod(
                "createClassLoader", String.class, String.class, String.class, ClassLoader.class, int.class,
                boolean.class, String.class);
        final ClassLoader loader = (ClassLoader) createClassLoader.invoke(null, Os.getenv("SYSTEMSERVERCLASSPATH"), null,
                null, ClassLoader.getSystemClassLoader(), 0, true, null);
        final Class<?> displayControl = loader.loadClass("com.android.server.display.DisplayControl");
        final Method loadLibrary = Runtime.class.getDeclaredMethod("loadLibrary0", Class.class, String.class);
        loadLibrary.setAccessible(true);
        loadLibrary.invoke(Runtime.getRuntime(), displayControl, "android_servers");
        return displayControl;
    }
}
