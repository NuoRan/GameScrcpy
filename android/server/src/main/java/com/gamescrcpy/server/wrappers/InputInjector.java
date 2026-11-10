package com.gamescrcpy.server.wrappers;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Build;
import android.view.InputEvent;
import com.gamescrcpy.server.Log;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.function.Consumer;

public final class InputInjector {

    public static final int MODE_ASYNC = 0;
    public static final int MODE_WAIT_FOR_FINISH = 2;

    private final InputManager manager;
    private final Method inject;
    private final Method setDisplayId;
    private final Consumer<String> permissionDenied;
    private boolean permissionReported;

    private InputInjector(InputManager manager, Method inject, Method setDisplayId, Consumer<String> permissionDenied) {
        this.manager = manager;
        this.inject = inject;
        this.setDisplayId = setDisplayId;
        this.permissionDenied = permissionDenied;
    }

    public static InputInjector create(Consumer<String> permissionDenied) throws PlatformException {
        try {
            final Context context = ProcessEnvironment.context();
            InputManager manager = context != null ? context.getSystemService(InputManager.class) : null;
            if (manager == null) {
                manager = (InputManager) InputManager.class.getMethod("getInstance").invoke(null);
            }
            final Method inject = InputManager.class.getMethod("injectInputEvent", InputEvent.class, int.class);
            final Method setDisplayId = Build.VERSION.SDK_INT >= 29
                    ? InputEvent.class.getMethod("setDisplayId", int.class) : null;
            return new InputInjector(manager, inject, setDisplayId, permissionDenied);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("input injection is unavailable", e);
        }
    }

    public boolean inject(InputEvent event, int displayId, int mode) {
        try {
            if (setDisplayId != null) {
                setDisplayId.invoke(event, displayId);
            } else if (displayId != 0) {
                return false;
            }
            final boolean injected = (boolean) inject.invoke(manager, event, mode);
            if (!injected && Log.enabled(Log.Level.DEBUG)) {
                Log.d("the input dispatcher refused " + event);
            }
            return injected;
        } catch (InvocationTargetException e) {
            if (e.getCause() instanceof SecurityException denied && !permissionReported) {
                permissionReported = true;
                permissionDenied.accept(String.valueOf(denied.getMessage()));
            }
            return false;
        } catch (ReflectiveOperationException | RuntimeException e) {
            return false;
        }
    }
}
