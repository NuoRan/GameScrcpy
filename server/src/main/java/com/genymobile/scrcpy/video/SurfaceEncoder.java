package com.genymobile.scrcpy.video;

import com.genymobile.scrcpy.AndroidVersions;
import com.genymobile.scrcpy.AsyncProcessor;
import com.genymobile.scrcpy.Options;
import com.genymobile.scrcpy.device.ConfigurationException;
import com.genymobile.scrcpy.device.Size;
import com.genymobile.scrcpy.device.IStreamer;
import com.genymobile.scrcpy.util.Codec;
import com.genymobile.scrcpy.util.CodecOption;
import com.genymobile.scrcpy.util.CodecUtils;
import com.genymobile.scrcpy.util.IO;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.util.LogUtils;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Surface;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class SurfaceEncoder implements AsyncProcessor {

    private static final int DEFAULT_I_FRAME_INTERVAL = 1; // seconds (reduced from 2s for ultra-low latency recovery)
    private static final String KEY_MAX_FPS_TO_ENCODER = "max-fps-to-encoder";

    // ABR (Average Bitrate) 控制参数
    private static final long ABR_WINDOW_MS = 500; // 码率统计窗口 500ms
    private static final float ABR_OVERSHOOT = 1.2f; // 超过目标 20% 时降码率
    private static final float ABR_UNDERSHOOT = 0.7f; // 低于目标 70% 时升码率
    private static final float ABR_MIN_RATIO = 0.25f; // 最低降至目标的 25%

    // Keep the values in descending order
    private static final int[] MAX_SIZE_FALLBACK = { 2560, 1920, 1600, 1280, 1024, 800 };
    private static final int MAX_CONSECUTIVE_ERRORS = 3;

    private final SurfaceCapture capture;
    private final IStreamer streamer;
    private final String encoderName;
    private final List<CodecOption> codecOptions;
    private final int videoBitRate;    // 初始码率 (来自 Options)
    private final float maxFps;        // 初始帧率 (来自 Options)
    private final boolean downsizeOnError;

    // 运行时可变参数 (线程安全)
    private volatile int runtimeBitRate;   // 当前生效码率 (ABR 基准)
    private volatile float runtimeMaxFps;  // 当前生效帧率
    // pendingMaxFps/pendingMaxSize: 非0 表示有待应用的参数变更 (需重启编码器)
    private volatile float pendingMaxFps;  // 0 = 无变更
    private volatile int pendingMaxSize;   // 0 = 无变更, -1 = 原始
    private final AtomicBoolean streamingPaused = new AtomicBoolean(false);

    private boolean firstFrameSent;
    private int consecutiveErrors;

    private Thread thread;
    private final AtomicBoolean stopped = new AtomicBoolean();
    private final AtomicInteger pendingBitRate = new AtomicInteger(0); // 0 = no change pending
    private volatile MediaCodec runningCodec;

    private final CaptureReset reset = new CaptureReset();
    private boolean useSafeCodecFormat;

    public SurfaceEncoder(SurfaceCapture capture, IStreamer streamer, Options options) {
        this.capture = capture;
        this.streamer = streamer;
        this.videoBitRate = options.getVideoBitRate();
        this.maxFps = options.getMaxFps();
        this.runtimeBitRate = this.videoBitRate;
        this.runtimeMaxFps = this.maxFps;
        this.codecOptions = options.getVideoCodecOptions();
        this.encoderName = options.getVideoEncoder();
        this.downsizeOnError = options.getDownsizeOnError();
    }

    private void streamCapture() throws IOException, ConfigurationException {
        Codec codec = streamer.getCodec();
        MediaCodec mediaCodec = createMediaCodec(codec, encoderName);
        useSafeCodecFormat = false;

        capture.init(reset);

        try {
            boolean alive;
            boolean headerWritten = false;

            do {
                reset.consumeReset(); // If a capture reset was requested, it is implicitly fulfilled

                // 应用待处理的运行时参数变更 (FPS / maxSize)
                float newFps = pendingMaxFps;
                if (newFps > 0 || newFps == -1) { // -1 表示设为 unlimited
                    runtimeMaxFps = newFps == -1 ? 0 : newFps;
                    pendingMaxFps = 0;
                    Ln.i("Runtime FPS changed to " + (runtimeMaxFps > 0 ? (int)runtimeMaxFps : "unlimited"));
                }
                int newMs = pendingMaxSize;
                if (newMs != 0) {
                    int actualSize = newMs == -1 ? 0 : newMs; // -1 → 0 (原始分辨率)
                    capture.setMaxSize(actualSize);
                    pendingMaxSize = 0;
                    Ln.i("Runtime maxSize changed to " + (actualSize > 0 ? actualSize : "original"));
                }

                capture.prepare();
                Size size = capture.getSize();
                if (!headerWritten) {
                    streamer.writeVideoHeader(size);
                    headerWritten = true;
                }

                MediaFormat format = createFormat(codec.getMimeType(), runtimeBitRate, runtimeMaxFps, codecOptions, !useSafeCodecFormat);
                format.setInteger(MediaFormat.KEY_WIDTH, size.getWidth());
                format.setInteger(MediaFormat.KEY_HEIGHT, size.getHeight());

                Ln.i("Configuring encoder: " + size.getWidth() + "x" + size.getHeight()
                    + " mode=" + (useSafeCodecFormat ? "safe" : "optimized")
                    + " bitrate=" + runtimeBitRate + " fps=" + (runtimeMaxFps > 0 ? runtimeMaxFps : 60));

                Surface surface = null;
                boolean mediaCodecStarted = false;
                boolean captureStarted = false;
                try {
                    mediaCodec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
                    surface = mediaCodec.createInputSurface();

                    capture.start(surface);
                    captureStarted = true;

                    mediaCodec.start();
                    mediaCodecStarted = true;
                    runningCodec = mediaCodec;

                    // Set the MediaCodec instance to "interrupt" (by signaling an EOS) on reset
                    reset.setRunningMediaCodec(mediaCodec);

                    if (stopped.get()) {
                        alive = false;
                    } else {
                        boolean resetRequested = reset.consumeReset();
                        if (!resetRequested) {
                            // If a reset is requested during encode(), it will interrupt the encoding by an
                            // EOS
                            encode(mediaCodec, streamer);
                        }
                        // The capture might have been closed internally (for example if the camera is
                        // disconnected)
                        alive = !stopped.get() && !capture.isClosed();
                    }
                } catch (IllegalArgumentException e) {
                    if (!useSafeCodecFormat) {
                        Ln.w("Encoder rejected optimized format, retrying with safe format");
                        useSafeCodecFormat = true;
                        alive = true;
                        continue;
                    }
                    Ln.e("Capture/encoding error: " + e.getClass().getName() + ": " + e.getMessage());
                    if (!prepareRetry(size)) {
                        throw e;
                    }
                    alive = true;
                } catch (IllegalStateException | IOException e) {
                    if (IO.isBrokenPipe(e)) {
                        // Do not retry on broken pipe, which is expected on close because the socket is
                        // closed by the client
                        throw e;
                    }
                    Ln.e("Capture/encoding error: " + e.getClass().getName() + ": " + e.getMessage());
                    if (!prepareRetry(size)) {
                        throw e;
                    }
                    alive = true;
                } finally {
                    runningCodec = null;
                    reset.setRunningMediaCodec(null);
                    if (captureStarted) {
                        capture.stop();
                    }
                    if (mediaCodecStarted) {
                        try {
                            mediaCodec.stop();
                        } catch (IllegalStateException e) {
                            // ignore (just in case)
                        }
                    }
                    mediaCodec.reset();
                    if (surface != null) {
                        surface.release();
                    }
                }
            } while (alive);
        } finally {
            mediaCodec.release();
            capture.release();
        }
    }

    private boolean prepareRetry(Size currentSize) {
        if (firstFrameSent) {
            ++consecutiveErrors;
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                // Definitively fail
                return false;
            }

            // Wait a bit to increase the probability that retrying will fix the problem
            SystemClock.sleep(50);
            return true;
        }

        if (!downsizeOnError) {
            // Must fail immediately
            return false;
        }

        // Downsizing on error is only enabled if an encoding failure occurs before the
        // first frame (downsizing later could be surprising)

        int newMaxSize = chooseMaxSizeFallback(currentSize);
        if (newMaxSize == 0) {
            // Must definitively fail
            return false;
        }

        boolean accepted = capture.setMaxSize(newMaxSize);
        if (!accepted) {
            return false;
        }

        // Retry with a smaller size
        Ln.i("Retrying with -m" + newMaxSize + "...");
        return true;
    }

    private static int chooseMaxSizeFallback(Size failedSize) {
        int currentMaxSize = Math.max(failedSize.getWidth(), failedSize.getHeight());
        for (int value : MAX_SIZE_FALLBACK) {
            if (value < currentMaxSize) {
                // We found a smaller value to reduce the video size
                return value;
            }
        }
        // No fallback, fail definitively
        return 0;
    }

    private void encode(MediaCodec codec, IStreamer streamer) throws IOException {
        MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();

        // ABR 控制状态
        long abrWindowStart = SystemClock.elapsedRealtime();
        long abrWindowBytes = 0;
        int abrCurrentBitrate = runtimeBitRate;

        // 网络拥塞反馈（可选）
        BitrateControl bitrateControl = (streamer instanceof BitrateControl) ? (BitrateControl) streamer : null;

        // 10ms 超时，允许检测状态变化，更快响应 EOS/reset 信号
        final long DEQUEUE_TIMEOUT_US = 10_000; // 10ms

        boolean eos;
        do {
            // 检查是否有待处理的运行时码率调整
            int newBr = pendingBitRate.getAndSet(0);
            if (newBr > 0) {
                try {
                    Bundle params = new Bundle();
                    params.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, newBr);
                    codec.setParameters(params);
                    runtimeBitRate = newBr;    // 更新 ABR 基准
                    abrCurrentBitrate = newBr;
                    Ln.i("Runtime bitrate changed to " + newBr / 1000 + " kbps");
                } catch (IllegalStateException e) {
                    Ln.w("Failed to set runtime bitrate: " + e.getMessage());
                }
            }

            int outputBufferId = codec.dequeueOutputBuffer(bufferInfo, DEQUEUE_TIMEOUT_US);

            try {
                eos = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                if (outputBufferId >= 0 && bufferInfo.size > 0) {
                    ByteBuffer codecBuffer = codec.getOutputBuffer(outputBufferId);

                    boolean isConfig = (bufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
                    if (!isConfig) {
                        firstFrameSent = true;
                        consecutiveErrors = 0;
                    }

                    // 暂停模式: 仍然 dequeue 以保持编码器活跃，但不发送数据
                    if (!streamingPaused.get()) {
                        streamer.writePacket(codecBuffer, bufferInfo);
                    }

                    // =================== ABR 码率控制 ===================
                    if (!isConfig && !streamingPaused.get()) {
                        abrWindowBytes += bufferInfo.size;
                        long now = SystemClock.elapsedRealtime();
                        long elapsed = now - abrWindowStart;

                        if (elapsed >= ABR_WINDOW_MS) {
                            int targetBr = runtimeBitRate; // 使用运行时码率作为 ABR 基准
                            // 计算窗口内实际码率 (bps)
                            long actualBitrate = abrWindowBytes * 8 * 1000 / elapsed;
                            float ratio = (float) actualBitrate / targetBr;

                            int newTarget;
                            if (ratio > ABR_OVERSHOOT) {
                                // 实际码率超标，按比例降低编码器目标
                                newTarget = (int) (abrCurrentBitrate / ratio);
                            } else if (ratio < ABR_UNDERSHOOT) {
                                // 实际码率偏低，逐步恢复（+10%）
                                newTarget = Math.min(targetBr, (int) (abrCurrentBitrate * 1.1f));
                            } else {
                                // 在目标范围内，保持不变
                                newTarget = abrCurrentBitrate;
                            }

                            // Clamp 到合理范围
                            newTarget = Math.max((int) (targetBr * ABR_MIN_RATIO),
                                    Math.min(targetBr, newTarget));

                            // 叠加网络拥塞反馈：取 ABR 和网络建议的较小值
                            if (bitrateControl != null) {
                                int networkSuggested = bitrateControl.getSuggestedBitrate(targetBr);
                                newTarget = Math.min(newTarget, networkSuggested);
                            }

                            // 码率变化超过 5% 才实际调整，避免频繁设置
                            if (Math.abs(newTarget - abrCurrentBitrate) > abrCurrentBitrate * 0.05f) {
                                try {
                                    Bundle params = new Bundle();
                                    params.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, newTarget);
                                    codec.setParameters(params);
                                    abrCurrentBitrate = newTarget;
                                    Ln.d("ABR adjust: " + abrCurrentBitrate / 1000 + " kbps"
                                            + " (actual=" + actualBitrate / 1000 + " kbps)");
                                } catch (IllegalStateException e) {
                                    // 部分编码器不支持动态码率
                                }
                            }

                            // 重置统计窗口
                            abrWindowStart = now;
                            abrWindowBytes = 0;
                        }
                    }
                }
            } finally {
                if (outputBufferId >= 0) {
                    codec.releaseOutputBuffer(outputBufferId, false);
                }
            }
        } while (!eos);
    }

    private static MediaCodec createMediaCodec(Codec codec, String encoderName)
            throws IOException, ConfigurationException {
        if (encoderName != null) {
            Ln.d("Creating encoder by name: '" + encoderName + "'");
            try {
                MediaCodec mediaCodec = MediaCodec.createByCodecName(encoderName);
                String mimeType = Codec.getMimeType(mediaCodec);
                if (!codec.getMimeType().equals(mimeType)) {
                    Ln.e("Video encoder type for \"" + encoderName + "\" (" + mimeType + ") does not match codec type ("
                            + codec.getMimeType() + ")");
                    throw new ConfigurationException("Incorrect encoder type: " + encoderName);
                }
                return mediaCodec;
            } catch (IllegalArgumentException e) {
                Ln.e("Video encoder '" + encoderName + "' for " + codec.getName() + " not found\n"
                        + LogUtils.buildVideoEncoderListMessage());
                throw new ConfigurationException("Unknown encoder: " + encoderName);
            } catch (IOException e) {
                Ln.e("Could not create video encoder '" + encoderName + "' for " + codec.getName() + "\n"
                        + LogUtils.buildVideoEncoderListMessage());
                throw e;
            }
        }

        try {
            MediaCodec mediaCodec = MediaCodec.createEncoderByType(codec.getMimeType());
            Ln.d("Using video encoder: '" + mediaCodec.getName() + "'");
            return mediaCodec;
        } catch (IOException | IllegalArgumentException e) {
            Ln.e("Could not create default video encoder for " + codec.getName() + "\n"
                    + LogUtils.buildVideoEncoderListMessage());
            throw e;
        }
    }

        private static MediaFormat createFormat(String videoMimeType, int bitRate, float maxFps,
            List<CodecOption> codecOptions, boolean optimized) {
        MediaFormat format = new MediaFormat();
        format.setString(MediaFormat.KEY_MIME, videoMimeType);
        format.setInteger(MediaFormat.KEY_BIT_RATE, bitRate);
        // must be present to configure the encoder, but does not impact the actual
        // frame rate, which is variable
        format.setInteger(MediaFormat.KEY_FRAME_RATE, maxFps > 0 ? (int) maxFps : 60);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, DEFAULT_I_FRAME_INTERVAL);

        if (optimized) {
            // COLOR_RANGE_LIMITED: 仅在优化模式设置，部分设备编码器不支持会抛 IllegalArgumentException
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_24_ANDROID_7_0) {
                format.setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_LIMITED);
            }
            // 低延迟编码配置：实时优先级
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_23_ANDROID_6_0) {
                format.setInteger(MediaFormat.KEY_PRIORITY, 0); // 0 = realtime priority
            }
            // Android 11+ (API 30): 显式请求低延迟编码
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_30_ANDROID_11) {
                format.setInteger(MediaFormat.KEY_LATENCY, 0); // 最低延迟
            }
            // 请求最大操作速率，禁止编码器降频节能
            if (Build.VERSION.SDK_INT >= AndroidVersions.API_23_ANDROID_6_0) {
                format.setInteger(MediaFormat.KEY_OPERATING_RATE, Short.MAX_VALUE);
            }
            // 高通/联发科私有低延迟标志（不支持的设备会忽略）
            try {
                format.setInteger("vendor.low-latency.enable", 1);
            } catch (Exception ignored) {
            }
            try {
                format.setInteger("vendor.rtc-ext-enc-low-latency.enable", 1);
            } catch (Exception ignored) {
            }

            // 禁止 B 帧，消除帧重排序延迟
            try {
                format.setInteger(MediaFormat.KEY_MAX_B_FRAMES, 0);
            } catch (Exception ignored) {
            }

            // H.264 Baseline Profile：无 B 帧，最低延迟
            if (videoMimeType.equals(MediaFormat.MIMETYPE_VIDEO_AVC)) {
                try {
                    format.setInteger(MediaFormat.KEY_PROFILE,
                            MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline);
                    // Baseline Profile 只支持 Level 对齐的 slice
                    format.setInteger(MediaFormat.KEY_LEVEL,
                            MediaCodecInfo.CodecProfileLevel.AVCLevel51);
                } catch (Exception ignored) {
                    // 部分设备不支持显式设置 Profile，忽略
                }
            }

            // 厂商私有低延迟标志 (Qualcomm/Samsung/MTK)
            try {
                format.setInteger("vendor.qti-ext-enc-low-latency.enable", 1);
            } catch (Exception ignored) {
            }
            try {
                format.setInteger("vendor.samsung.enc.low-latency.enable", 1);
            } catch (Exception ignored) {
            }
            try {
                format.setInteger("vendor.mtk.enc.low-latency.enable", 1);
            } catch (Exception ignored) {
            }
            try {
                format.setInteger("low-latency", 1);
            } catch (Exception ignored) {
            }

            // CBR 模式：更稳定的码率输出
            try {
                format.setInteger(MediaFormat.KEY_BITRATE_MODE,
                        MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR);
            } catch (Exception ignored) {
            }
        }

        // 不设置 KEY_REPEAT_PREVIOUS_FRAME_AFTER，有新帧才编码发送，没帧不发
        if (maxFps > 0) {
            // The key existed privately before Android 10:
            // <https://android.googlesource.com/platform/frameworks/base/+/625f0aad9f7a259b6881006ad8710adce57d1384%5E%21/>
            // <https://github.com/Genymobile/scrcpy/issues/488#issuecomment-567321437>
            format.setFloat(KEY_MAX_FPS_TO_ENCODER, maxFps);
        }

        if (codecOptions != null) {
            for (CodecOption option : codecOptions) {
                String key = option.getKey();
                Object value = option.getValue();
                CodecUtils.setCodecOption(format, key, value);
                Ln.d("Video codec option set: " + key + " (" + value.getClass().getSimpleName() + ") = " + value);
            }
        }

        Ln.d("Video encoder format mode: " + (optimized ? "optimized" : "safe"));

        return format;
    }

    @Override
    public void start(TerminationListener listener) {
        thread = new Thread(() -> {
            // Some devices (Meizu) deadlock if the video encoding thread has no Looper
            // <https://github.com/Genymobile/scrcpy/issues/4143>
            Looper.prepare();

            boolean fatalError = false;
            try {
                streamCapture();
            } catch (ConfigurationException e) {
                // Do not print stack trace, a user-friendly error-message has already been
                // logged
                fatalError = true;
            } catch (IOException e) {
                // Broken pipe is expected on close, because the socket is closed by the client
                if (!IO.isBrokenPipe(e)) {
                    Ln.e("Video encoding error", e);
                    fatalError = true;
                }
            } finally {
                Ln.d("Screen streaming stopped");
                listener.onTerminated(fatalError);
            }
        }, "video");
        thread.start();
    }

    @Override
    public void stop() {
        if (thread != null) {
            stopped.set(true);
            reset.reset();
        }
    }

    @Override
    public void join() throws InterruptedException {
        if (thread != null) {
            thread.join();
        }
    }

    /**
     * 运行时调整码率（线程安全，可从任意线程调用）
     * 下一个编码周期生效。
     */
    public void setRuntimeBitRate(int bitrate) {
        pendingBitRate.set(bitrate);
    }

    /**
     * 运行时调整视频参数（线程安全）。
     * 码率立即生效；帧率/分辨率变更会触发编码器重启（约 100-300ms 中断）。
     *
     * @param bitrate  新码率 (bps)，0 = 不变
     * @param maxFps   新帧率，0 = 不限制，0xFFFF = 不变
     * @param maxSize  新分辨率上限 (px)，0 = 原始，0xFFFF = 不变
     */
    public void setRuntimeVideoParams(int bitrate, int maxFps, int maxSize) {
        // 码率可热更新，无需重启
        if (bitrate > 0) {
            pendingBitRate.set(bitrate);
            runtimeBitRate = bitrate;
        }

        boolean needRestart = false;

        if (maxFps != 0xFFFF) {
            float target = maxFps;
            if (target != runtimeMaxFps) {
                pendingMaxFps = target == 0 ? -1 : target; // -1 sentinel = unlimited
                needRestart = true;
            }
        }
        if (maxSize != 0xFFFF) {
            int target = maxSize;
            // 用 -1 表示 "原始"，因为 0 是 pendingMaxSize 的 "无变更" 标记
            pendingMaxSize = target == 0 ? -1 : target;
            needRestart = true;
        }

        if (needRestart) {
            Ln.i("Encoder restart requested (fps/resolution change)");
            reset.reset();
        }
    }

    /**
     * 暂停/恢复视频流传输（线程安全）。
     * 暂停后编码器仍运行，但不发送数据到客户端，节省带宽。
     */
    public void setStreamingPaused(boolean paused) {
        streamingPaused.set(paused);
        Ln.i("Video streaming " + (paused ? "PAUSED" : "RESUMED"));
    }
}
