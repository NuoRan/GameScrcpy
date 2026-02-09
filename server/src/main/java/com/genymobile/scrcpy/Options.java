/*
 * Options.java - 服务端启动选项
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 基于 Genymobile/scrcpy 二次开发
 */

package com.genymobile.scrcpy;

import com.genymobile.scrcpy.device.Orientation;
import com.genymobile.scrcpy.util.CodecOption;
import com.genymobile.scrcpy.util.Ln;
import com.genymobile.scrcpy.video.VideoCodec;
import com.genymobile.scrcpy.wrappers.WindowManager;

import android.graphics.Rect;
import android.util.Pair;

import java.util.List;
import java.util.Locale;

/**
 * 服务端启动选项
 * <p>
 * 解析客户端传递的启动参数，包括:
 * <ul>
 *   <li>视频参数: 编码、码率、帧率、分辨率</li>
 *   <li>控制参数: 显示ID、触摸显示、保持唤醒</li>
 *   <li>传输参数: KCP/TCP 模式、端口配置</li>
 * </ul>
 */
public class Options {

    private Ln.Level logLevel = Ln.Level.DEBUG;
    private int scid = -1; // 31-bit non-negative value, or -1
    private boolean video = true;
    private int maxSize;
    private VideoCodec videoCodec = VideoCodec.H264;
    private int videoBitRate = 8000000;
    private float maxFps;
    private float angle;
    private boolean tunnelForward;
    private Rect crop;
    private boolean control = true;
    private int displayId;
    private boolean showTouches;
    private boolean stayAwake;
    private int screenOffTimeout = -1;
    private int displayImePolicy = -1;
    private List<CodecOption> videoCodecOptions;

    private String videoEncoder;
    private boolean powerOffScreenOnClose;
    private boolean clipboardAutosync = true;
    private boolean downsizeOnError = true;
    private boolean cleanup = true;
    private boolean powerOn = true;

    private Orientation.Lock captureOrientationLock = Orientation.Lock.Unlocked;
    private Orientation captureOrientation = Orientation.Orient0;

    // Options not used by the scrcpy client, but useful to use scrcpy-server directly
    private boolean sendDeviceMeta = true; // send device name and size
    private boolean sendFrameMeta = true; // send PTS so that the client may record properly
    private boolean sendDummyByte = true; // write a byte on start to detect connection issues
    private boolean sendCodecMeta = true; // write the codec metadata before the stream

    // KCP video streaming (WiFi mode)
    private boolean useKcp = false;  // false = TCP mode, true = KCP mode
    private int kcpPort = 27185;
    private int kcpControlPort = 27186;  // Control channel port (kcpPort + 1)
    private String clientIp = "";

    public Ln.Level getLogLevel() {
        return logLevel;
    }

    public int getScid() {
        return scid;
    }

    public boolean getVideo() {
        return video;
    }

    public int getMaxSize() {
        return maxSize;
    }

    public VideoCodec getVideoCodec() {
        return videoCodec;
    }

    public int getVideoBitRate() {
        return videoBitRate;
    }

    public float getMaxFps() {
        return maxFps;
    }

    public float getAngle() {
        return angle;
    }

    public boolean isTunnelForward() {
        return tunnelForward;
    }

    public Rect getCrop() {
        return crop;
    }

    public boolean getControl() {
        return control;
    }

    public int getDisplayId() {
        return displayId;
    }

    public boolean getShowTouches() {
        return showTouches;
    }

    public boolean getStayAwake() {
        return stayAwake;
    }

    public int getScreenOffTimeout() {
        return screenOffTimeout;
    }

    public int getDisplayImePolicy() {
        return displayImePolicy;
    }

    public List<CodecOption> getVideoCodecOptions() {
        return videoCodecOptions;
    }

    public String getVideoEncoder() {
        return videoEncoder;
    }

    public boolean getPowerOffScreenOnClose() {
        return this.powerOffScreenOnClose;
    }

    public boolean getClipboardAutosync() {
        return clipboardAutosync;
    }

    public boolean getDownsizeOnError() {
        return downsizeOnError;
    }

    public boolean getCleanup() {
        return cleanup;
    }

    public boolean getPowerOn() {
        return powerOn;
    }

    public Orientation getCaptureOrientation() {
        return captureOrientation;
    }

    public Orientation.Lock getCaptureOrientationLock() {
        return captureOrientationLock;
    }

    public boolean getSendDeviceMeta() {
        return sendDeviceMeta;
    }

    public boolean getSendFrameMeta() {
        return sendFrameMeta;
    }

    public boolean getSendDummyByte() {
        return sendDummyByte;
    }

    public boolean getSendCodecMeta() {
        return sendCodecMeta;
    }

    public boolean getUseKcp() {
        return useKcp;
    }

    public int getKcpPort() {
        return kcpPort;
    }

    public int getKcpControlPort() {
        return kcpControlPort;
    }

    public String getClientIp() {
        return clientIp;
    }

    @SuppressWarnings("MethodLength")
    public static Options parse(String... args) {
        if (args.length < 1) {
            throw new IllegalArgumentException("Missing client version");
        }

        String clientVersion = args[0];
        if (!clientVersion.equals(BuildConfig.VERSION_NAME)) {
            throw new IllegalArgumentException(
                    "The server version (" + BuildConfig.VERSION_NAME + ") does not match the client " + "(" + clientVersion + ")");
        }

        Options options = new Options();

        for (int i = 1; i < args.length; ++i) {
            String arg = args[i];
            int equalIndex = arg.indexOf('=');
            if (equalIndex == -1) {
                throw new IllegalArgumentException("Invalid key=value pair: \"" + arg + "\"");
            }
            String key = arg.substring(0, equalIndex);
            String value = arg.substring(equalIndex + 1);
            switch (key) {
                case "scid":
                    int scid = Integer.parseInt(value, 0x10);
                    if (scid < -1) {
                        throw new IllegalArgumentException("scid may not be negative (except -1 for 'none'): " + scid);
                    }
                    options.scid = scid;
                    break;
                case "log_level":
                    options.logLevel = Ln.Level.valueOf(value.toUpperCase(Locale.ENGLISH));
                    break;
                case "video":
                    options.video = Boolean.parseBoolean(value);
                    break;
                case "audio":
                    // ignored - audio disabled
                    break;
                case "video_codec":
                    VideoCodec videoCodec = VideoCodec.findByName(value);
                    if (videoCodec == null) {
                        throw new IllegalArgumentException("Video codec " + value + " not supported");
                    }
                    options.videoCodec = videoCodec;
                    break;
                case "max_size":
                    options.maxSize = Integer.parseInt(value) & ~7; // multiple of 8
                    break;
                case "video_bit_rate":
                    options.videoBitRate = Integer.parseInt(value);
                    break;
                case "max_fps":
                    options.maxFps = parseFloat("max_fps", value);
                    break;
                case "angle":
                    options.angle = parseFloat("angle", value);
                    break;
                case "tunnel_forward":
                    options.tunnelForward = Boolean.parseBoolean(value);
                    break;
                case "crop":
                    if (!value.isEmpty()) {
                        options.crop = parseCrop(value);
                    }
                    break;
                case "control":
                    options.control = Boolean.parseBoolean(value);
                    break;
                case "display_id":
                    options.displayId = Integer.parseInt(value);
                    break;
                case "show_touches":
                    options.showTouches = Boolean.parseBoolean(value);
                    break;
                case "stay_awake":
                    options.stayAwake = Boolean.parseBoolean(value);
                    break;
                case "screen_off_timeout":
                    options.screenOffTimeout = Integer.parseInt(value);
                    if (options.screenOffTimeout < -1) {
                        throw new IllegalArgumentException("Invalid screen off timeout: " + options.screenOffTimeout);
                    }
                    break;
                case "video_codec_options":
                    options.videoCodecOptions = CodecOption.parse(value);
                    break;
                case "video_encoder":
                    if (!value.isEmpty()) {
                        options.videoEncoder = value;
                    }
                    break;
                case "power_off_on_close":
                    options.powerOffScreenOnClose = Boolean.parseBoolean(value);
                    break;
                case "clipboard_autosync":
                    options.clipboardAutosync = Boolean.parseBoolean(value);
                    break;
                case "downsize_on_error":
                    options.downsizeOnError = Boolean.parseBoolean(value);
                    break;
                case "cleanup":
                    options.cleanup = Boolean.parseBoolean(value);
                    break;
                case "power_on":
                    options.powerOn = Boolean.parseBoolean(value);
                    break;
                case "capture_orientation":
                    Pair<Orientation.Lock, Orientation> pair = parseCaptureOrientation(value);
                    options.captureOrientationLock = pair.first;
                    options.captureOrientation = pair.second;
                    break;
                case "display_ime_policy":
                    options.displayImePolicy = parseDisplayImePolicy(value);
                    break;
                case "send_device_meta":
                    options.sendDeviceMeta = Boolean.parseBoolean(value);
                    break;
                case "send_frame_meta":
                    options.sendFrameMeta = Boolean.parseBoolean(value);
                    break;
                case "send_dummy_byte":
                    options.sendDummyByte = Boolean.parseBoolean(value);
                    break;
                case "send_codec_meta":
                    options.sendCodecMeta = Boolean.parseBoolean(value);
                    break;
                case "use_kcp":
                    options.useKcp = Boolean.parseBoolean(value);
                    break;
                case "kcp_port":
                    options.kcpPort = Integer.parseInt(value);
                    break;
                case "kcp_control_port":
                    options.kcpControlPort = Integer.parseInt(value);
                    break;
                case "client_ip":
                    options.clientIp = value;
                    break;
                case "raw_stream":
                    boolean rawStream = Boolean.parseBoolean(value);
                    if (rawStream) {
                        options.sendDeviceMeta = false;
                        options.sendFrameMeta = false;
                        options.sendDummyByte = false;
                        options.sendCodecMeta = false;
                    }
                    break;
                default:
                    Ln.w("Unknown server option: " + key);
                    break;
            }
        }

        return options;
    }

    private static Rect parseCrop(String crop) {
        // input format: "width:height:x:y"
        String[] tokens = crop.split(":");
        if (tokens.length != 4) {
            throw new IllegalArgumentException("Crop must contains 4 values separated by colons: \"" + crop + "\"");
        }
        int width = Integer.parseInt(tokens[0]);
        int height = Integer.parseInt(tokens[1]);
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("Invalid crop size: " + width + "x" + height);
        }
        int x = Integer.parseInt(tokens[2]);
        int y = Integer.parseInt(tokens[3]);
        if (x < 0 || y < 0) {
            throw new IllegalArgumentException("Invalid crop offset: " + x + ":" + y);
        }
        return new Rect(x, y, x + width, y + height);
    }

    private static float parseFloat(String key, String value) {
        try {
            return Float.parseFloat(value);
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("Invalid float value for " + key + ": \"" + value + "\"");
        }
    }

    private static Pair<Orientation.Lock, Orientation> parseCaptureOrientation(String value) {
        if (value.isEmpty()) {
            throw new IllegalArgumentException("Empty capture orientation string");
        }

        Orientation.Lock lock;
        if (value.charAt(0) == '@') {
            // Consume '@'
            value = value.substring(1);
            if (value.isEmpty()) {
                // Only '@': lock to the initial orientation (orientation is unused)
                return Pair.create(Orientation.Lock.LockedInitial, Orientation.Orient0);
            }
            lock = Orientation.Lock.LockedValue;
        } else {
            lock = Orientation.Lock.Unlocked;
        }

        return Pair.create(lock, Orientation.getByName(value));
    }

    private static int parseDisplayImePolicy(String value) {
        switch (value) {
            case "local":
                return WindowManager.DISPLAY_IME_POLICY_LOCAL;
            case "fallback":
                return WindowManager.DISPLAY_IME_POLICY_FALLBACK_DISPLAY;
            case "hide":
                return WindowManager.DISPLAY_IME_POLICY_HIDE;
            default:
                throw new IllegalArgumentException("Invalid display IME policy: " + value);
        }
    }
}
