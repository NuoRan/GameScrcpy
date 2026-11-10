package com.gamescrcpy.server.wrappers;

import android.annotation.SuppressLint;
import android.app.ActivityOptions;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.os.IBinder;
import android.os.IInterface;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.server.device.AppService;
import com.gamescrcpy.server.device.ForegroundTracker;
import java.io.ByteArrayOutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class Apps implements AppService.Platform {

    private static final int USER_CURRENT = -2;
    private static final int INCLUDE_DISABLED =
            PackageManager.MATCH_DISABLED_COMPONENTS | PackageManager.MATCH_DISABLED_UNTIL_USED_COMPONENTS;
    private final PackageManager packages;
    private final IInterface activities;
    private final Method startActivity;
    private final Method forceStop;

    private Apps(PackageManager packages, IInterface activities, Method startActivity, Method forceStop) {
        this.packages = packages;
        this.activities = activities;
        this.startActivity = startActivity;
        this.forceStop = forceStop;
    }

    public static Apps create() throws PlatformException {
        final Context context = ProcessEnvironment.context();
        if (context == null) {
            throw new PlatformException("no context to reach the package manager");
        }
        final PackageManager packages = context.getPackageManager();
        if (packages == null) {
            throw new PlatformException("the package manager is unavailable");
        }
        final IInterface activities = SystemServices.get("activity", "android.app.IActivityManager");
        try {
            final Method start = activities.getClass().getMethod("startActivityAsUser",
                    Class.forName("android.app.IApplicationThread"), String.class, Intent.class, String.class,
                    IBinder.class, String.class, int.class, int.class, Class.forName("android.app.ProfilerInfo"),
                    Bundle.class, int.class);
            final Method stop = activities.getClass().getMethod("forceStopPackage", String.class, int.class);
            return new Apps(packages, activities, start, stop);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new PlatformException("apps cannot be started from the shell", e);
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    @SuppressLint("QueryPermissionsNeeded")
    public List<AppService.App> launchable(boolean includeSystem) throws AppService.Failure {
        try {
            final Set<String> launchers = new HashSet<>();
            entries(launchers, Intent.CATEGORY_LAUNCHER);
            entries(launchers, Intent.CATEGORY_LEANBACK_LAUNCHER);
            final List<AppService.App> apps = new ArrayList<>();
            for (PackageInfo info : packages.getInstalledPackages(INCLUDE_DISABLED)) {
                final ApplicationInfo application = info.applicationInfo;
                if (application == null || !launchers.contains(info.packageName)) {
                    continue;
                }
                final boolean system = (application.flags & ApplicationInfo.FLAG_SYSTEM) != 0;
                if (system && !includeSystem) {
                    continue;
                }
                final boolean game = ForegroundTracker.isGame(application.category, application.flags);
                apps.add(new AppService.App(info.packageName, String.valueOf(application.loadLabel(packages)),
                        info.getLongVersionCode(), system, application.enabled, game));
            }
            return apps;
        } catch (RuntimeException e) {
            throw new AppService.Failure(ErrorCode.INTERNAL_ERROR, "cannot list the apps: " + e, e);
        }
    }

    @Override
    public byte[] icon(String packageName, int sizePx) throws AppService.Failure {
        final Drawable icon;
        try {
            icon = packages.getApplicationIcon(packageName);
        } catch (PackageManager.NameNotFoundException e) {
            throw new AppService.Failure(ErrorCode.NOT_FOUND, packageName + " is not installed", e);
        }
        try {
            final Bitmap bitmap = Bitmap.createBitmap(sizePx, sizePx, Bitmap.Config.ARGB_8888);
            final Canvas canvas = new Canvas(bitmap);
            icon.setBounds(0, 0, sizePx, sizePx);
            icon.draw(canvas);
            final ByteArrayOutputStream png = new ByteArrayOutputStream();
            final boolean written = bitmap.compress(Bitmap.CompressFormat.PNG, 100, png);
            bitmap.recycle();
            if (!written) {
                throw new AppService.Failure(ErrorCode.INTERNAL_ERROR, "the icon of " + packageName
                        + " could not be encoded");
            }
            return png.toByteArray();
        } catch (RuntimeException e) {
            throw new AppService.Failure(ErrorCode.INTERNAL_ERROR, "cannot draw the icon of " + packageName + ": "
                    + e, e);
        }
    }

    @Override
    @SuppressLint("QueryPermissionsNeeded")
    public void start(String packageName, int displayId, boolean stopFirst) throws AppService.Failure {
        Intent intent = packages.getLaunchIntentForPackage(packageName);
        if (intent == null) {
            intent = packages.getLeanbackLaunchIntentForPackage(packageName);
        }
        if (intent == null) {
            throw new AppService.Failure(ErrorCode.NOT_FOUND, packageName + " is not installed or has no launcher entry");
        }
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        final ActivityOptions options = ActivityOptions.makeBasic();
        options.setLaunchDisplayId(displayId);
        final int result;
        try {
            if (stopFirst) {
                forceStop.invoke(activities, packageName, USER_CURRENT);
            }
            result = (int) startActivity.invoke(activities, null, ProcessEnvironment.SHELL_PACKAGE, intent, null, null,
                    null, 0, 0, null, options.toBundle(), USER_CURRENT);
        } catch (InvocationTargetException e) {
            throw new AppService.Failure(ErrorCode.INTERNAL_ERROR, "the system refused to start " + packageName + ": "
                    + e.getCause(), e);
        } catch (ReflectiveOperationException | RuntimeException e) {
            throw new AppService.Failure(ErrorCode.INTERNAL_ERROR, "cannot start " + packageName + ": " + e, e);
        }
        if (result < 0) {
            throw new AppService.Failure(ErrorCode.INTERNAL_ERROR, "the system refused to start " + packageName
                    + " (result " + result + ")");
        }
    }

    @SuppressWarnings("deprecation")
    @SuppressLint("QueryPermissionsNeeded")
    private void entries(Set<String> into, String category) {
        final Intent launcher = new Intent(Intent.ACTION_MAIN).addCategory(category);
        for (ResolveInfo resolved : packages.queryIntentActivities(launcher, INCLUDE_DISABLED)) {
            if (resolved.activityInfo != null) {
                into.add(resolved.activityInfo.packageName);
            }
        }
    }
}
