/*
 * Server.java - scrcpy 服务端入口点
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 基于 Genymobile/scrcpy 二次开发
 */

package com.genymobile.scrcpy;

import com.genymobile.scrcpy.device.ConfigurationException;
import com.genymobile.scrcpy.session.KcpSession;
import com.genymobile.scrcpy.session.ScrcpySession;
import com.genymobile.scrcpy.session.TcpSession;
import com.genymobile.scrcpy.util.Ln;

import android.annotation.SuppressLint;
import android.os.Build;
import android.os.Looper;

import java.io.File;
import java.io.IOException;
import java.lang.reflect.Field;

/**
 * scrcpy 服务端入口点
 * <p>
 * 支持两种传输模式:
 * <ul>
 *   <li>WiFi 模式 (use_kcp=true): 使用 KCP/UDP，低延迟</li>
 *   <li>USB 模式 (use_kcp=false): 使用 TCP + adb forward，兼容性好</li>
 * </ul>
 * <p>
 * 优化: 使用策略模式消除 KCP/TCP 代码重复
 */
public final class Server {

    public static final String SERVER_PATH;

    static {
        String[] classPaths = System.getProperty("java.class.path").split(File.pathSeparator);
        SERVER_PATH = classPaths[0];
    }

    private Server() {
        // not instantiable
    }

    /**
     * 创建会话 - 工厂方法
     */
    private static ScrcpySession createSession(Options options) {
        if (options.getUseKcp()) {
            return new KcpSession(options);
        } else {
            return new TcpSession(options);
        }
    }

    /**
     * 运行 scrcpy 会话
     */
    private static void scrcpy(Options options) throws IOException, ConfigurationException {
        // 版本检查
        if (Build.VERSION.SDK_INT < AndroidVersions.API_29_ANDROID_10) {
            if (options.getDisplayImePolicy() != -1) {
                Ln.e("Display IME policy is not supported before Android 10");
                throw new ConfigurationException("Display IME policy is not supported");
            }
        }

        // 使用策略模式创建并运行会话
        try (ScrcpySession session = createSession(options)) {
            session.run();
        }
    }

    private static void prepareMainLooper() {
        Looper.prepare();
        synchronized (Looper.class) {
            try {
                @SuppressLint("DiscouragedPrivateApi")
                Field field = Looper.class.getDeclaredField("sMainLooper");
                field.setAccessible(true);
                field.set(null, Looper.myLooper());
            } catch (ReflectiveOperationException e) {
                throw new AssertionError(e);
            }
        }
    }

    public static void main(String... args) {
        int status = 0;
        try {
            internalMain(args);
        } catch (Throwable t) {
            Ln.e(t.getMessage(), t);
            status = 1;
        } finally {
            System.exit(status);
        }
    }

    private static void internalMain(String... args) throws Exception {
        Thread.UncaughtExceptionHandler defaultHandler = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            Ln.e("Exception on thread " + t, e);
            if (defaultHandler != null) {
                defaultHandler.uncaughtException(t, e);
            }
        });

        prepareMainLooper();

        Options options = Options.parse(args);

        Ln.disableSystemStreams();
        Ln.initLogLevel(options.getLogLevel());

        Ln.i("Device: [" + Build.MANUFACTURER + "] " + Build.BRAND + " " + Build.MODEL + " (Android " + Build.VERSION.RELEASE + ")");

        try {
            scrcpy(options);
        } catch (ConfigurationException e) {
            // Do not print stack trace, a user-friendly error-message has already been logged
        }
    }
}
