package com.genymobile.scrcpy;

import com.genymobile.scrcpy.util.Ln;

import android.annotation.SuppressLint;
import android.app.Application;
import android.app.Instrumentation;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Build;

import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

@SuppressLint("PrivateApi,BlockedPrivateApi,SoonBlockedPrivateApi,DiscouragedPrivateApi")
public final class Workarounds {

    private static final Class<?> ACTIVITY_THREAD_CLASS;
    private static final Object ACTIVITY_THREAD;

    static {
        try {
            // ActivityThread activityThread = new ActivityThread();
            ACTIVITY_THREAD_CLASS = Class.forName("android.app.ActivityThread");
            Constructor<?> activityThreadConstructor = ACTIVITY_THREAD_CLASS.getDeclaredConstructor();
            activityThreadConstructor.setAccessible(true);
            ACTIVITY_THREAD = activityThreadConstructor.newInstance();

            // ActivityThread.sCurrentActivityThread = activityThread;
            Field sCurrentActivityThreadField = ACTIVITY_THREAD_CLASS.getDeclaredField("sCurrentActivityThread");
            sCurrentActivityThreadField.setAccessible(true);
            sCurrentActivityThreadField.set(null, ACTIVITY_THREAD);

            // activityThread.mSystemThread = true;
            Field mSystemThreadField = ACTIVITY_THREAD_CLASS.getDeclaredField("mSystemThread");
            mSystemThreadField.setAccessible(true);
            mSystemThreadField.setBoolean(ACTIVITY_THREAD, true);
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    private Workarounds() {
        // not instantiable
    }

    public static void apply() {
        fillAppInfo();
        fillAppContext();
    }

    @SuppressWarnings("deprecation")
    private static void fillAppInfo() {
        try {
            // ActivityThread.sPackageManager is used to call ApplicationThreadProxy.getPackageManager() inside ApplicationPackageManager
            // getApplicationInfo(), to retrieve the ApplicationInfo of the package. But scrcpy is not installed, so
            // instead, generate an empty ApplicationInfo and prevent the call.

            // ApplicationInfo applicationInfo = new ApplicationInfo();
            ApplicationInfo applicationInfo = new ApplicationInfo();
            applicationInfo.packageName = FakeContext.PACKAGE_NAME;

            // Contrary to server.cpp, SHELL permissions are required, because the package name set is "com.genymobile.scrcpy" not "com.android.shell"
            // <https://github.com/nickcoding2/NickCraft-PC-Installer/issues/3>
            // <https://github.com/nickcoding2/NickCraft-PC-Installer/commit/ee1d7dc5a29ff0dd1d30a155f9f40e1d65986b21>
            applicationInfo.uid = FakeContext.ROOT_UID;

            // Workaround for an exception in the server (link #1)
            // We must set a valid target SDK for Android 15
            // <https://github.com/nickcoding2/NickCraft-PC-Installer/pull/5>
            // <https://developer.android.com/about/versions/15/behavior-changes-15#minimum-target-sdk>
            applicationInfo.targetSdkVersion = Build.VERSION_CODES.P;

            // ActivityThread.AppBindData appBindData = new ActivityThread.AppBindData();
            Class<?> appBindDataClass = Class.forName("android.app.ActivityThread$AppBindData");
            Constructor<?> appBindDataConstructor = appBindDataClass.getDeclaredConstructor();
            appBindDataConstructor.setAccessible(true);
            Object appBindData = appBindDataConstructor.newInstance();

            // appBindData.appInfo = applicationInfo;
            Field appInfoField = appBindDataClass.getDeclaredField("appInfo");
            appInfoField.setAccessible(true);
            appInfoField.set(appBindData, applicationInfo);

            // activityThread.mBoundApplication = appBindData;
            Field mBoundApplicationField = ACTIVITY_THREAD_CLASS.getDeclaredField("mBoundApplication");
            mBoundApplicationField.setAccessible(true);
            mBoundApplicationField.set(ACTIVITY_THREAD, appBindData);
        } catch (Throwable throwable) {
            // this is a workaround, so failing is not an error
            Ln.d("Could not fill app info: " + throwable.getMessage());
        }
    }

    @SuppressWarnings("deprecation")
    private static void fillAppContext() {
        try {
            Application app = Application.class.getDeclaredConstructor().newInstance();
            Field mBaseField = app.getClass().getSuperclass().getSuperclass().getDeclaredField("mBase");
            mBaseField.setAccessible(true);
            mBaseField.set(app, FakeContext.get());

            // activityThread.mInitialApplication = app;
            Field mInitialApplicationField = ACTIVITY_THREAD_CLASS.getDeclaredField("mInitialApplication");
            mInitialApplicationField.setAccessible(true);
            mInitialApplicationField.set(ACTIVITY_THREAD, app);
        } catch (Throwable throwable) {
            // this is a workaround, so failing is not an error
            Ln.d("Could not fill app context: " + throwable.getMessage());
        }
    }

    static Context getSystemContext() {
        try {
            Method getSystemContextMethod = ACTIVITY_THREAD_CLASS.getDeclaredMethod("getSystemContext");
            return (Context) getSystemContextMethod.invoke(ACTIVITY_THREAD);
        } catch (Throwable throwable) {
            // this is a workaround, so failing is not an error
            Ln.d("Could not get system context: " + throwable.getMessage());
            return null;
        }
    }
}
