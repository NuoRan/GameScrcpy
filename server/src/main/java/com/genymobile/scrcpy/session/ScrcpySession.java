package com.genymobile.scrcpy.session;

import com.genymobile.scrcpy.AsyncProcessor;
import com.genymobile.scrcpy.CleanUp;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.Workarounds;
import com.genymobile.scrcpy.audio.AudioCapture;
import com.genymobile.scrcpy.audio.AudioCodec;
import com.genymobile.scrcpy.audio.AudioDirectCapture;
import com.genymobile.scrcpy.audio.AudioEncoder;
import com.genymobile.scrcpy.audio.AudioPlaybackCapture;
import com.genymobile.scrcpy.audio.AudioRawRecorder;
import com.genymobile.scrcpy.audio.AudioSource;
import com.genymobile.scrcpy.auxiliary.IAuxChannel;
import com.genymobile.scrcpy.control.Controller;
import com.genymobile.scrcpy.control.IControlChannel;
import com.genymobile.scrcpy.device.ConfigurationException;
import com.genymobile.scrcpy.device.Device;
import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.device.Streamer;
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
    protected IAuxChannel auxChannel;
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

            // 4.5. 初始化音频流 (如果启用)
            if (options.getAudio()) {
                Streamer audioStreamer = createAudioStreamer();
                if (audioStreamer != null) {
                    AudioCodec audioCodec = options.getAudioCodec();
                    AudioSource audioSource = options.getAudioSource();
                    AudioCapture audioCapture;
                    if (audioSource.isDirect()) {
                        audioCapture = new AudioDirectCapture(audioSource);
                    } else {
                        audioCapture = new AudioPlaybackCapture(options.getAudioDup());
                    }

                    AsyncProcessor audioRecorder;
                    if (audioCodec == AudioCodec.RAW) {
                        audioRecorder = new AudioRawRecorder(audioCapture, audioStreamer);
                    } else {
                        audioRecorder = new AudioEncoder(audioCapture, audioStreamer, options);
                    }
                    asyncProcessors.add(audioRecorder);
                    Ln.i("Audio streaming started (codec=" + audioCodec.getName() + ")");
                }
            }

            // 5. 初始化视频流 (如果启用)
            if (options.getVideo()) {
                videoStreamer = createVideoStreamer();
                if (videoStreamer != null) {
                    SurfaceCapture surfaceCapture = new ScreenCapture(controller, options);
                    SurfaceEncoder surfaceEncoder = new SurfaceEncoder(surfaceCapture, videoStreamer, options);
                    asyncProcessors.add(surfaceEncoder);

                    // 将编码器的运行时码率调整回调绑定到 Controller (仅码率，不影响控制通道)
                    if (controller != null) {
                        controller.setBitrateCallback(surfaceEncoder::setRuntimeBitRate);
                    }

                    // 6. 初始化辅助通道（独立于控制通道）
                    auxChannel = createAuxChannel();
                    if (auxChannel != null) {
                        auxChannel.setCallback(new IAuxChannel.Callback() {
                            @Override
                            public void onVideoParamsChanged(int bitrate, int maxFps, int maxSize) {
                                surfaceEncoder.setRuntimeVideoParams(bitrate, maxFps, maxSize);
                            }

                            @Override
                            public void onStreamingChanged(boolean streaming) {
                                surfaceEncoder.setStreamingPaused(!streaming);
                            }
                        });
                        auxChannel.start();
                        Ln.i("Auxiliary channel started");
                    }

                    Ln.i("Video streaming started");
                }
            }

            // 7. 执行会话特定初始化
            onSessionInitialized();

            // 8. 启动所有处理器
            Completion completion = new Completion(asyncProcessors.size());
            for (AsyncProcessor asyncProcessor : asyncProcessors) {
                asyncProcessor.start((fatalError) -> {
                    completion.addCompleted(fatalError);
                });
            }

            // 9. 进入主循环
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
     * 创建音频流发送器 - 子类实现
     * 返回 null 表示不支持音频
     */
    protected abstract Streamer createAudioStreamer() throws IOException;

    /**
     * 创建控制通道 - 子类实现
     */
    protected abstract IControlChannel createControlChannel() throws IOException;

    /**
     * 创建辅助通道 - 子类实现
     * 独立于控制通道，用于视频参数/音频/剪贴板等非输入类命令
     */
    protected abstract IAuxChannel createAuxChannel() throws IOException;

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

        // 2. 停止辅助通道
        if (auxChannel != null) {
            auxChannel.stop();
        }

        // 3. 停止所有处理器
        for (AsyncProcessor asyncProcessor : asyncProcessors) {
            asyncProcessor.stop();
        }

        // 4. 停止 OpenGL
        OpenGLRunner.quit();

        // 5. 子类特定清理
        onCleanup();

        // 6. 等待线程结束
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
