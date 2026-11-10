package com.gamescrcpy.server.wrappers;

import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.os.Binder;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.IInterface;
import android.os.Parcel;
import android.os.RemoteException;
import com.gamescrcpy.server.Log;
import com.gamescrcpy.server.device.ForegroundTracker;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.IntSupplier;

public final class ForegroundApps implements AutoCloseable {

    public interface Listener {
        void foregroundChanged(String packageName, String activity, boolean game);
    }

    private static final String LISTENER = "android.app.ITaskStackListener";
    private static final int TASKS_PER_DISPLAY_LOOKUP = 32;

    private final ActivityManager activities;
    private final PackageManager packages;
    private final Listener listener;
    private final IntSupplier display;
    private final Field taskDisplay;
    private final HandlerThread thread = new HandlerThread("gs-foreground");
    private final Handler handler;
    private final AtomicBoolean pending = new AtomicBoolean();
    private Runnable unregister = () -> {
    };
    private String lookedUp = "";
    private boolean lookedUpGame;

    private ForegroundApps(ActivityManager activities, PackageManager packages, Listener listener, IntSupplier display,
            Field taskDisplay) {
        this.activities = activities;
        this.packages = packages;
        this.listener = listener;
        this.display = display;
        this.taskDisplay = taskDisplay;
        thread.start();
        handler = new Handler(thread.getLooper());
    }

    public static ForegroundApps start(Listener listener, IntSupplier display) throws PlatformException {
        final Context context = ProcessEnvironment.context();
        if (context == null) {
            throw new PlatformException("no context to read the running tasks");
        }
        final ActivityManager activities = context.getSystemService(ActivityManager.class);
        if (activities == null) {
            throw new PlatformException("the activity manager is unavailable");
        }
        final boolean taskManager = Build.VERSION.SDK_INT >= 29;
        final IInterface tasks = SystemServices.get(taskManager ? "activity_task" : "activity",
                taskManager ? "android.app.IActivityTaskManager" : "android.app.IActivityManager");
        final ForegroundApps watcher = new ForegroundApps(activities, context.getPackageManager(), listener, display,
                taskDisplayField());
        try {
            final Class<?> listenerType = Class.forName(LISTENER);
            final Method asInterface = Class.forName(LISTENER + "$Stub").getMethod("asInterface", IBinder.class);
            final Object callback = asInterface.invoke(null, new StackListener(watcher::post));
            final Method register = tasks.getClass().getMethod("registerTaskStackListener", listenerType);
            final Method unregister = tasks.getClass().getMethod("unregisterTaskStackListener", listenerType);
            register.invoke(tasks, callback);
            watcher.unregister = () -> {
                try {
                    unregister.invoke(tasks, callback);
                } catch (ReflectiveOperationException | RuntimeException ignored) {
                }
            };
        } catch (ReflectiveOperationException | RuntimeException e) {
            watcher.thread.quitSafely();
            throw new PlatformException("cannot register a task stack listener", e);
        }
        watcher.post();
        return watcher;
    }

    public void refresh() {
        post();
    }

    @Override
    public void close() {
        unregister.run();
        thread.quitSafely();
    }

    private void post() {
        if (pending.compareAndSet(false, true)) {
            handler.post(() -> {
                pending.set(false);
                check();
            });
        }
    }

    private void check() {
        String packageName = "";
        String activity = "";
        try {
            final ComponentName top = topActivity();
            if (top != null) {
                packageName = top.getPackageName();
                activity = top.getClassName();
            }
        } catch (RuntimeException e) {
            Log.w("cannot read the foreground app", e);
            return;
        }
        listener.foregroundChanged(packageName, activity, isGame(packageName));
    }

    @SuppressWarnings("deprecation")
    private boolean isGame(String packageName) {
        if (packageName == null || packageName.isEmpty() || packages == null) {
            return false;
        }
        if (packageName.equals(lookedUp)) {
            return lookedUpGame;
        }
        boolean game = false;
        try {
            final ApplicationInfo info = packages.getApplicationInfo(packageName, 0);
            game = ForegroundTracker.isGame(info.category, info.flags);
        } catch (PackageManager.NameNotFoundException | RuntimeException e) {
            Log.d("no application info for " + packageName + ": " + e);
        }
        lookedUp = packageName;
        lookedUpGame = game;
        return game;
    }

    @SuppressWarnings("deprecation")
    private ComponentName topActivity() {
        final List<ActivityManager.RunningTaskInfo> running =
                activities.getRunningTasks(taskDisplay != null ? TASKS_PER_DISPLAY_LOOKUP : 1);
        if (running == null) {
            return null;
        }
        final int wanted = display.getAsInt();
        for (ActivityManager.RunningTaskInfo task : running) {
            if (taskDisplay == null || displayOf(task) == wanted) {
                return task.topActivity;
            }
        }
        return null;
    }

    private int displayOf(ActivityManager.RunningTaskInfo task) {
        try {
            return taskDisplay.getInt(task);
        } catch (IllegalAccessException e) {
            return 0;
        }
    }

    private static Field taskDisplayField() {
        if (Build.VERSION.SDK_INT < 29) {
            return null;
        }
        try {
            return Class.forName("android.app.TaskInfo").getField("displayId");
        } catch (ReflectiveOperationException | RuntimeException e) {
            Log.w("the foreground app cannot be told apart by display", e);
            return null;
        }
    }

    private static final class StackListener extends Binder {
        private final Runnable changed;

        StackListener(Runnable changed) {
            this.changed = changed;
            attachInterface(null, LISTENER);
        }

        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code >= FIRST_CALL_TRANSACTION && code <= LAST_CALL_TRANSACTION) {
                changed.run();
                return true;
            }
            return super.onTransact(code, data, reply, flags);
        }
    }
}
