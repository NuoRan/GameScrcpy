package com.gamescrcpy.server.wrappers;

import android.content.Context;
import android.content.res.Configuration;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;
import android.view.IDisplayWindowListener;
import java.lang.reflect.Method;

public final class DisplayWatcher implements AutoCloseable {

    public interface Listener {
        void displayChanged(int displayId);
    }

    private final Runnable unregister;

    private DisplayWatcher(Runnable unregister) {
        this.unregister = unregister;
    }

    public static DisplayWatcher start(Listener listener) throws PlatformException {
        return Build.VERSION.SDK_INT >= 34 ? throughWindowManager(listener) : throughDisplayManager(listener);
    }

    @Override
    public void close() {
        unregister.run();
    }

    private static DisplayWatcher throughDisplayManager(Listener listener) throws PlatformException {
        final Context context = ProcessEnvironment.context();
        if (context == null) {
            throw new PlatformException("no context to reach the DisplayManager");
        }
        final DisplayManager manager = context.getSystemService(DisplayManager.class);
        final HandlerThread thread = new HandlerThread("gs-display");
        thread.start();
        final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
            @Override
            public void onDisplayAdded(int displayId) {
            }

            @Override
            public void onDisplayRemoved(int displayId) {
            }

            @Override
            public void onDisplayChanged(int displayId) {
                listener.displayChanged(displayId);
            }
        };
        manager.registerDisplayListener(displayListener, new Handler(thread.getLooper()));
        return new DisplayWatcher(() -> {
            manager.unregisterDisplayListener(displayListener);
            thread.quitSafely();
        });
    }

    private static DisplayWatcher throughWindowManager(Listener listener) throws PlatformException {
        final IInterface windowManager = SystemServices.get("window", "android.view.IWindowManager");
        final IDisplayWindowListener.Stub stub = new IDisplayWindowListener.Stub() {
            @Override
            public void onDisplayAdded(int displayId) {
            }

            @Override
            public void onDisplayConfigurationChanged(int displayId, Configuration newConfig) {
                listener.displayChanged(displayId);
            }

            @Override
            public void onDisplayRemoved(int displayId) {
            }

            @Override
            public boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
                try {
                    return super.onTransact(code, data, reply, flags);
                } catch (AbstractMethodError e) {
                    if (reply != null) {
                        reply.writeNoException();
                    }
                    return true;
                }
            }
        };
        try {
            final Method register = windowManager.getClass().getMethod("registerDisplayWindowListener",
                    IDisplayWindowListener.class);
            final Method unregister = windowManager.getClass().getMethod("unregisterDisplayWindowListener",
                    IDisplayWindowListener.class);
            register.invoke(windowManager, stub);
            return new DisplayWatcher(() -> {
                try {
                    unregister.invoke(windowManager, stub);
                } catch (ReflectiveOperationException | RuntimeException ignored) {
                }
            });
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot register a display window listener", e);
        }
    }
}
