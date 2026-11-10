package com.gamescrcpy.server.wrappers;

import android.annotation.SuppressLint;
import android.content.AttributionSource;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Process;
import com.gamescrcpy.server.Log;
import java.lang.reflect.Method;

public final class SystemSettings {

    private static final String AUTHORITY = "settings";
    private static final String TAG = "gamescrcpy";
    private static final String USER_KEY = "_user";
    private static final String VALUE_KEY = "value";
    private static final int USER_SYSTEM = 0;
    private static final int USER_CURRENT = -2;

    private enum CallShape { ATTRIBUTION, FEATURE, AUTHORITY, PACKAGE }

    private final IInterface activityManager;
    private final Method acquire;
    private final boolean acquireTagged;
    private final Method release;
    private final boolean releaseAsUser;
    private final Method call;
    private final CallShape shape;

    private SystemSettings(IInterface activityManager, Method acquire, boolean acquireTagged, Method release,
                           boolean releaseAsUser, Method call, CallShape shape) {
        this.activityManager = activityManager;
        this.acquire = acquire;
        this.acquireTagged = acquireTagged;
        this.release = release;
        this.releaseAsUser = releaseAsUser;
        this.call = call;
        this.shape = shape;
    }

    public static SystemSettings create() throws PlatformException {
        final IInterface manager = SystemServices.get("activity", "android.app.IActivityManager");
        final Class<?> type = manager.getClass();
        try {
            Method acquire = find(type, "getContentProviderExternal", String.class, int.class, IBinder.class,
                    String.class);
            final boolean tagged = acquire != null;
            if (acquire == null) {
                acquire = type.getMethod("getContentProviderExternal", String.class, int.class, IBinder.class);
            }
            Method release = find(type, "removeContentProviderExternalAsUser", String.class, IBinder.class,
                    int.class);
            final boolean asUser = release != null;
            if (release == null) {
                release = find(type, "removeContentProviderExternal", String.class, IBinder.class);
            }
            final Class<?> provider = Class.forName("android.content.IContentProvider");
            Method call = attributionClass() != null
                    ? find(provider, "call", attributionClass(), String.class, String.class, String.class, Bundle.class)
                    : null;
            CallShape shape = CallShape.ATTRIBUTION;
            if (call == null) {
                call = find(provider, "call", String.class, String.class, String.class, String.class, String.class,
                        Bundle.class);
                shape = CallShape.FEATURE;
            }
            if (call == null) {
                call = find(provider, "call", String.class, String.class, String.class, String.class, Bundle.class);
                shape = CallShape.AUTHORITY;
            }
            if (call == null) {
                call = provider.getMethod("call", String.class, String.class, String.class, Bundle.class);
                shape = CallShape.PACKAGE;
            }
            return new SystemSettings(manager, acquire, tagged, release, asUser, call, shape);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("the system settings cannot be reached here", e);
        }
    }

    public String getSystem(String name) throws PlatformException {
        final Bundle extras = new Bundle();
        extras.putInt(USER_KEY, USER_CURRENT);
        final Bundle result = call("GET_system", name, extras);
        return result == null ? null : result.getString(VALUE_KEY);
    }

    public void putSystem(String name, String value) throws PlatformException {
        final Bundle extras = new Bundle();
        extras.putInt(USER_KEY, USER_CURRENT);
        extras.putString(VALUE_KEY, value);
        call("PUT_system", name, extras);
    }

    private Bundle call(String method, String name, Bundle extras) throws PlatformException {
        final IBinder token = new Binder();
        final Object holder;
        try {
            holder = acquireTagged
                    ? acquire.invoke(activityManager, AUTHORITY, USER_SYSTEM, token, TAG)
                    : acquire.invoke(activityManager, AUTHORITY, USER_SYSTEM, token);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("cannot reach the settings provider", e);
        }
        if (holder == null) {
            throw new PlatformException("the settings provider is not running");
        }
        try {
            final Object provider = holder.getClass().getField("provider").get(holder);
            final Object result = switch (shape) {
                case ATTRIBUTION -> call.invoke(provider, shellAttribution(), AUTHORITY, method, name, extras);
                case FEATURE -> call.invoke(provider, ProcessEnvironment.SHELL_PACKAGE, null, AUTHORITY, method, name,
                        extras);
                case AUTHORITY -> call.invoke(provider, ProcessEnvironment.SHELL_PACKAGE, AUTHORITY, method, name,
                        extras);
                case PACKAGE -> call.invoke(provider, ProcessEnvironment.SHELL_PACKAGE, method, name, extras);
            };
            return (Bundle) result;
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("the settings provider refused " + method + " " + name, e);
        } finally {
            give(token);
        }
    }

    private void give(IBinder token) {
        if (release == null) {
            return;
        }
        try {
            if (releaseAsUser) {
                release.invoke(activityManager, AUTHORITY, token, USER_SYSTEM);
            } else {
                release.invoke(activityManager, AUTHORITY, token);
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.d("cannot let go of the settings provider: " + e);
        }
    }

    private static Class<?> attributionClass() {
        try {
            return Class.forName("android.content.AttributionSource");
        } catch (ClassNotFoundException e) {
            return null;
        }
    }

    @SuppressLint("NewApi")
    private static Object shellAttribution() {
        return new AttributionSource.Builder(Process.SHELL_UID).setPackageName(ProcessEnvironment.SHELL_PACKAGE)
                .build();
    }

    private static Method find(Class<?> type, String name, Class<?>... parameters) {
        try {
            return type.getMethod(name, parameters);
        } catch (NoSuchMethodException e) {
            return null;
        }
    }
}
