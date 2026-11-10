package com.gamescrcpy.server.wrappers;

import android.graphics.Rect;
import android.hardware.display.VirtualDisplay;
import android.os.Build;
import android.os.IBinder;
import android.view.Surface;
import com.gamescrcpy.server.device.DisplaySnapshot;
import java.lang.reflect.Method;

public final class DisplayMirror implements AutoCloseable {

    private static final String NAME = "gamescrcpy";

    private final VirtualDisplay virtualDisplay;
    private final IBinder displayToken;

    private DisplayMirror(VirtualDisplay virtualDisplay, IBinder displayToken) {
        this.virtualDisplay = virtualDisplay;
        this.displayToken = displayToken;
    }

    public static DisplayMirror create(DisplaySnapshot display, int width, int height, Surface surface)
            throws PlatformException {
        Throwable managerFailure = null;
        try {
            final Method create = android.hardware.display.DisplayManager.class.getMethod("createVirtualDisplay",
                    String.class, int.class, int.class, int.class, Surface.class);
            return new DisplayMirror(
                    (VirtualDisplay) create.invoke(null, NAME, width, height, display.displayId(), surface), null);
        } catch (NoSuchMethodException e) {
        } catch (ReflectiveOperationException | RuntimeException e) {
            managerFailure = e;
        }
        try {
            return new DisplayMirror(null, surfaceControlDisplay(display, width, height, surface));
        } catch (ReflectiveOperationException | RuntimeException e) {
            final PlatformException failure = new PlatformException("cannot mirror display " + display.displayId(), e);
            if (managerFailure != null) {
                failure.addSuppressed(managerFailure);
            }
            throw failure;
        }
    }

    @Override
    public void close() {
        if (virtualDisplay != null) {
            virtualDisplay.release();
        } else {
            try {
                Class.forName("android.view.SurfaceControl").getMethod("destroyDisplay", IBinder.class)
                        .invoke(null, displayToken);
            } catch (ReflectiveOperationException | RuntimeException ignored) {
            }
        }
    }

    private static IBinder surfaceControlDisplay(DisplaySnapshot display, int width, int height, Surface surface)
            throws ReflectiveOperationException {
        final Class<?> surfaceControl = Class.forName("android.view.SurfaceControl");
        final boolean secure = Build.VERSION.SDK_INT < 30
                || (Build.VERSION.SDK_INT == 30 && !"S".equals(Build.VERSION.CODENAME));
        final IBinder token = (IBinder) surfaceControl.getMethod("createDisplay", String.class, boolean.class)
                .invoke(null, NAME, secure);
        boolean configured = false;
        surfaceControl.getMethod("openTransaction").invoke(null);
        try {
            surfaceControl.getMethod("setDisplaySurface", IBinder.class, Surface.class).invoke(null, token, surface);
            surfaceControl.getMethod("setDisplayProjection", IBinder.class, int.class, Rect.class, Rect.class)
                    .invoke(null, token, 0, new Rect(0, 0, display.width(), display.height()),
                            new Rect(0, 0, width, height));
            surfaceControl.getMethod("setDisplayLayerStack", IBinder.class, int.class)
                    .invoke(null, token, display.layerStack());
            configured = true;
        } finally {
            surfaceControl.getMethod("closeTransaction").invoke(null);
            if (!configured) {
                surfaceControl.getMethod("destroyDisplay", IBinder.class).invoke(null, token);
            }
        }
        return token;
    }
}
