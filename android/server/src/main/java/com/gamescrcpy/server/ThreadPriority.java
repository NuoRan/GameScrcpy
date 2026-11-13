package com.gamescrcpy.server;

import android.os.Process;
import java.util.HashSet;
import java.util.Set;

public final class ThreadPriority {

    interface Setter {
        void set(int priority);
    }

    public static final int VIDEO = Process.THREAD_PRIORITY_VIDEO;
    public static final int AUDIO = Process.THREAD_PRIORITY_AUDIO;
    public static final int DEFAULT = Process.THREAD_PRIORITY_DEFAULT;

    private static final Setter PLATFORM = priority -> Process.setThreadPriority(priority);
    private static final Object lock = new Object();
    private static final Set<Integer> appliedLogged = new HashSet<>();
    private static boolean failureLogged;

    private ThreadPriority() {
    }

    public static void raise(int priority) {
        raise(priority, PLATFORM);
    }

    public static Runnable atDefault(Runnable body) {
        return () -> {
            raise(DEFAULT);
            body.run();
        };
    }

    static void raise(int priority, Setter setter) {
        try {
            setter.set(priority);
        } catch (RuntimeException e) {
            synchronized (lock) {
                if (!failureLogged) {
                    failureLogged = true;
                    Log.d("could not raise the thread priority to " + priority + ": " + e);
                }
            }
            return;
        }
        synchronized (lock) {
            if (appliedLogged.add(priority)) {
                Log.d("thread priority " + priority + " applied");
            }
        }
    }

    static void resetLogged() {
        synchronized (lock) {
            appliedLogged.clear();
            failureLogged = false;
        }
    }
}
