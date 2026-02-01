package com.genymobile.scrcpy.session;

import com.genymobile.scrcpy.AsyncProcessor;
import com.genymobile.scrcpy.CleanUp;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.Workarounds;
import com.genymobile.scrcpy.control.Controller;
import com.genymobile.scrcpy.control.IControlChannel;
import com.genymobile.scrcpy.device.ConfigurationException;
import com.genymobile.scrcpy.device.Device;
import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.opengl.OpenGLRunner;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.video.ScreenCapture;
import com.genymobile.scrcpy.video.SurfaceCapture;
import com.genymobile.scrcpy.video.SurfaceEncoder;

import android.os.Looper;

import java.io.Closeable;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * ScrcpySession - 抽象会话基类
 *
 * A-01 优化: 使用模板方法模式消除 scrcpyKcp() 和 scrcpyTcp() 的代码重复
 *
 * 子类只需实现:
 * - createVideoStreamer(): 创建视频流发送器 (KCP 或 TCP)
 * - createControlChannel(): 创建控制通道 (KCP 或 TCP)
 * - getSessionName(): 返回会话名称用于日志
 * - cleanup(): 清理会话特定资源
 */
public abstract class ScrcpySession implements Closeable {

    protected final Options options;
    protected final List<AsyncProcessor> asyncProcessors = new ArrayList<>();
    protected CleanUp cleanUp;
    protected Controller controller;
    protected IControlChannel controlChannel;
    protected IStreamer videoStreamer;

    public ScrcpySession(Options options) {
        this.options = options;
    }

    /**
     * 模板方法 - 运行会话
     */
    public final void run() throws IOException, ConfigurationException {
        // 0. 会话前置初始化 (子类可覆盖)
        beforeRun();

        // 1. 初始化清理器
        if (options.getCleanup()) {
            cleanUp = CleanUp.start(options);
        }

        // 2. 应用 workarounds
        Workarounds.apply();

        try {
            // 3. 打印设备信息
            Ln.i("Device: " + Device.getDeviceName());
            Ln.i("Session: " + getSessionName());

            // 4. 初始化控制通道 (如果启用)
            if (options.getControl()) {
                controlChannel = createControlChannel();
                if (controlChannel != null) {
                    controller = new Controller(controlChannel, cleanUp, options);
                    asyncProcessors.add(controller);
                    Ln.i("Control channel started");
                }
            }

            // 5. 初始化视频流 (如果启用)
            if (options.getVideo()) {
                videoStreamer = createVideoStreamer();
                if (videoStreamer != null) {
                    SurfaceCapture surfaceCapture = new ScreenCapture(controller, options);
                    SurfaceEncoder surfaceEncoder = new SurfaceEncoder(surfaceCapture, videoStreamer, options);
                    asyncProcessors.add(surfaceEncoder);
                    Ln.i("Video streaming started");
                }
            }

            // 6. 执行会话特定初始化
            onSessionInitialized();

            // 7. 启动所有处理器
            Completion completion = new Completion(asyncProcessors.size());
            for (AsyncProcessor asyncProcessor : asyncProcessors) {
                asyncProcessor.start((fatalError) -> {
                    completion.addCompleted(fatalError);
                });
            }

            // 8. 进入主循环
            Looper.loop();

        } finally {
            // 9. 清理资源
            cleanup();
        }
    }

    /**
     * 创建视频流发送器 - 子类实现
     */
    protected abstract IStreamer createVideoStreamer() throws IOException;

    /**
     * 创建控制通道 - 子类实现
     */
    protected abstract IControlChannel createControlChannel() throws IOException;

    /**
     * 获取会话名称 - 子类实现
     */
    protected abstract String getSessionName();

    /**
     * 会话运行前调用 - 子类可覆盖
     * 用于建立连接等前置操作
     */
    protected void beforeRun() throws IOException, ConfigurationException {
        // 默认空实现
    }

    /**
     * 会话初始化完成后调用 - 子类可覆盖
     */
    protected void onSessionInitialized() throws IOException {
        // 默认空实现
    }

    /**
     * 清理会话特定资源 - 子类可覆盖
     */
    protected void onCleanup() {
        // 默认空实现
    }

    /**
     * 通用清理逻辑
     */
    private void cleanup() {
        // 1. 中断清理线程
        if (cleanUp != null) {
            cleanUp.interrupt();
        }

        // 2. 停止所有处理器
        for (AsyncProcessor asyncProcessor : asyncProcessors) {
            asyncProcessor.stop();
        }

        // 3. 停止 OpenGL
        OpenGLRunner.quit();

        // 4. 子类特定清理
        onCleanup();

        // 5. 等待线程结束
        try {
            if (cleanUp != null) {
                cleanUp.join();
            }
            for (AsyncProcessor asyncProcessor : asyncProcessors) {
                asyncProcessor.join();
            }
            OpenGLRunner.join();
        } catch (InterruptedException e) {
            // ignore
        }
    }

    @Override
    public void close() throws IOException {
        cleanup();
    }

    /**
     * 完成计数器 - 用于协调多个处理器
     */
    private static class Completion {
        private int running;
        private boolean fatalError;

        Completion(int running) {
            this.running = running;
        }

        synchronized void addCompleted(boolean fatalError) {
            --running;
            if (fatalError) {
                this.fatalError = true;
            }
            if (running == 0 || this.fatalError) {
                Looper.getMainLooper().quitSafely();
            }
        }
    }
}
