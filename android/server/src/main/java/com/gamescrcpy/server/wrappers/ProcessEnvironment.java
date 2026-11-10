package com.gamescrcpy.server.wrappers;

import android.annotation.SuppressLint;
import android.app.Application;
import android.app.Instrumentation;
import android.content.AttributionSource;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.pm.ApplicationInfo;
import android.os.Build;
import android.os.Looper;
import android.os.Process;
import com.gamescrcpy.server.Log;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

public final class ProcessEnvironment {

    public static final String SHELL_PACKAGE = "com.android.shell";

    private static Class<?> activityThreadClass;
    private static Object activityThread;
    @SuppressLint("StaticFieldLeak")
    private static Context context;

    private ProcessEnvironment() {
    }

    @SuppressWarnings("deprecation")
    public static synchronized void prepare() {
        if (activityThread != null) {
            return;
        }
        Looper.prepareMainLooper();
        try {
            activityThreadClass = Class.forName("android.app.ActivityThread");
            final Constructor<?> constructor = activityThreadClass.getDeclaredConstructor();
            constructor.setAccessible(true);
            activityThread = constructor.newInstance();
            setField("sCurrentActivityThread", null, activityThread);
            setField("mSystemThread", activityThread, true);
        } catch (ReflectiveOperationException e) {
            Log.w("cannot create an ActivityThread", e);
            return;
        }
        if (Build.VERSION.SDK_INT >= 31) {
            fillConfigurationController();
        }
        if (!"ONYX".equalsIgnoreCase(Build.BRAND)) {
            fillApplicationInfo();
        }
        final Context system = systemContext();
        if (system != null) {
            context = new ShellContext(system);
            fillApplication();
        }
    }

    public static synchronized Context context() {
        return context;
    }

    private static Context systemContext() {
        try {
            final Method method = activityThreadClass.getDeclaredMethod("getSystemContext");
            return (Context) method.invoke(activityThread);
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.w("cannot get the system context", e);
            return null;
        }
    }

    private static void fillConfigurationController() {
        try {
            final Class<?> controllerClass = Class.forName("android.app.ConfigurationController");
            final Class<?> internalClass = Class.forName("android.app.ActivityThreadInternal");
            final Constructor<?> constructor = controllerClass.getDeclaredConstructor(internalClass);
            constructor.setAccessible(true);
            setField("mConfigurationController", activityThread, constructor.newInstance(activityThread));
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.d("no configuration controller: " + e);
        }
    }

    private static void fillApplicationInfo() {
        try {
            final Class<?> bindDataClass = Class.forName("android.app.ActivityThread$AppBindData");
            final Constructor<?> constructor = bindDataClass.getDeclaredConstructor();
            constructor.setAccessible(true);
            final Object bindData = constructor.newInstance();
            final ApplicationInfo info = new ApplicationInfo();
            info.packageName = SHELL_PACKAGE;
            final Field appInfo = bindDataClass.getDeclaredField("appInfo");
            appInfo.setAccessible(true);
            appInfo.set(bindData, info);
            setField("mBoundApplication", activityThread, bindData);
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.d("no bound application info: " + e);
        }
    }

    private static void fillApplication() {
        try {
            final Application application = Instrumentation.newApplication(Application.class, context);
            setField("mInitialApplication", activityThread, application);
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.d("no application object: " + e);
        }
    }

    private static void setField(String name, Object target, Object value) throws ReflectiveOperationException {
        final Field field = activityThreadClass.getDeclaredField(name);
        field.setAccessible(true);
        field.set(target, value);
    }

    private static final class ShellContext extends ContextWrapper {

        ShellContext(Context base) {
            super(base);
        }

        @Override
        public String getPackageName() {
            return SHELL_PACKAGE;
        }

        @Override
        public String getOpPackageName() {
            return SHELL_PACKAGE;
        }

        @SuppressLint("NewApi")
        @Override
        public AttributionSource getAttributionSource() {
            return new AttributionSource.Builder(Process.SHELL_UID).setPackageName(SHELL_PACKAGE).build();
        }

        @Override
        public Context getApplicationContext() {
            return this;
        }
    }
}
