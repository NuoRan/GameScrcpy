package com.gamescrcpy.protocol;

import com.gamescrcpy.protocol.Enums.AudioCodec;
import com.gamescrcpy.protocol.Enums.AudioStatus;
import com.gamescrcpy.protocol.Enums.ChannelId;
import com.gamescrcpy.protocol.Enums.CopyKey;
import com.gamescrcpy.protocol.Enums.ErrorCode;
import com.gamescrcpy.protocol.Enums.ImeAction;
import com.gamescrcpy.protocol.Enums.KeyAction;
import com.gamescrcpy.protocol.Enums.KeyHintShape;
import com.gamescrcpy.protocol.Enums.NoticeLevel;
import com.gamescrcpy.protocol.Enums.OrientationMode;
import com.gamescrcpy.protocol.Enums.OverlayStatus;
import com.gamescrcpy.protocol.Enums.ScreenshotFormat;
import com.gamescrcpy.protocol.Enums.ScreenshotProblem;
import com.gamescrcpy.protocol.Enums.SystemPanelAction;
import com.gamescrcpy.protocol.Enums.TouchAction;
import com.gamescrcpy.protocol.Enums.VideoCodec;
import com.gamescrcpy.protocol.Enums.VideoSource;

public final class Messages {

    private Messages() {
    }

    public static final class Display implements Struct {
        public long displayId;
        public int width;
        public int height;
        public int rotation;
        public int densityDpi;
        public long refreshMilliHz;
        public int safeLeft;
        public int safeTop;
        public int safeRight;
        public int safeBottom;

        @Override
        public void visit(FieldVisitor v) {
            displayId = v.u32("displayId", displayId);
            width = v.u16("width", width);
            height = v.u16("height", height);
            rotation = v.u8Range("rotation", rotation, 0, 3);
            densityDpi = v.u16("densityDpi", densityDpi);
            refreshMilliHz = v.u32("refreshMilliHz", refreshMilliHz);
            safeLeft = v.u16("safeLeft", safeLeft);
            safeTop = v.u16("safeTop", safeTop);
            safeRight = v.u16("safeRight", safeRight);
            safeBottom = v.u16("safeBottom", safeBottom);
        }
    }

    public static final class Encoder implements Struct {
        public VideoCodec codec = VideoCodec.H264;
        public String name = "";
        public boolean hardware;

        @Override
        public void visit(FieldVisitor v) {
            codec = v.enumeration("codec", codec, VideoCodec.CODEC);
            name = v.str8("name", name, 128);
            hardware = v.bool("hardware", hardware);
        }
    }

    public static final class AppEntry implements Struct {
        public static final int SYSTEM = 1;
        public static final int ENABLED = 2;
        public static final int GAME = 4;

        public String packageName = "";
        public String label = "";
        public long versionCode;
        public int flags;

        @Override
        public void visit(FieldVisitor v) {
            packageName = v.str8("package", packageName, 255);
            label = v.str8("label", label, 255);
            versionCode = v.u64("versionCode", versionCode);
            flags = v.flags8(flags, "system", "enabled", "game");
        }
    }

    public static final class TouchPoint implements Struct {
        public TouchAction action = TouchAction.DOWN;
        public int pointerId;
        public int x;
        public int y;
        public int pressure;

        public TouchPoint() {
        }

        public TouchPoint(TouchAction action, int pointerId, int x, int y, int pressure) {
            this.action = action;
            this.pointerId = pointerId;
            this.x = x;
            this.y = y;
            this.pressure = pressure;
        }

        @Override
        public void visit(FieldVisitor v) {
            action = v.enumeration("action", action, TouchAction.CODEC);
            pointerId = v.u8Range("pointerId", pointerId, 0, Constants.MAX_POINTER_ID);
            x = v.u16("x", x);
            y = v.u16("y", y);
            pressure = v.u16("pressure", pressure);
        }
    }

    public static final class VideoSessionInfo implements Struct {
        public VideoCodec codec = VideoCodec.H264;
        public int frameWidth;
        public int frameHeight;
        public long displayId;
        public int displayWidth;
        public int displayHeight;
        public int rotation;

        @Override
        public void visit(FieldVisitor v) {
            codec = v.enumeration("codec", codec, VideoCodec.CODEC);
            frameWidth = v.u16("frameWidth", frameWidth);
            frameHeight = v.u16("frameHeight", frameHeight);
            displayId = v.u32("displayId", displayId);
            displayWidth = v.u16("displayWidth", displayWidth);
            displayHeight = v.u16("displayHeight", displayHeight);
            rotation = v.u8Range("rotation", rotation, 0, 3);
        }
    }

    public static final class AudioSessionInfo implements Struct {
        public AudioCodec codec = AudioCodec.OPUS;
        public long sampleRate;
        public int channels;

        @Override
        public void visit(FieldVisitor v) {
            codec = v.enumeration("codec", codec, AudioCodec.CODEC);
            sampleRate = v.u32("sampleRate", sampleRate);
            channels = v.u8("channels", channels);
        }
    }

    public static final class ServerHello implements Message {
        public int protocolVersion;
        public byte[] nonce = new byte[16];

        @Override public MessageType type() { return MessageType.SERVER_HELLO; }

        @Override
        public void visit(FieldVisitor v) {
            v.magic(Constants.MAGIC);
            protocolVersion = v.u16("protocolVersion", protocolVersion);
            nonce = v.bytes("nonce", nonce, 16);
        }
    }

    public static final class ClientHello implements Message {
        public int protocolVersion;
        public String clientVersion = "";
        public byte[] proof = new byte[32];

        @Override public MessageType type() { return MessageType.CLIENT_HELLO; }

        @Override
        public void visit(FieldVisitor v) {
            protocolVersion = v.u16("protocolVersion", protocolVersion);
            clientVersion = v.str8("clientVersion", clientVersion, 32);
            proof = v.bytes("proof", proof, 32);
        }
    }

    public static final class Welcome implements Message {
        public String serverVersion = "";
        public String manufacturer = "";
        public String model = "";
        public String androidRelease = "";
        public int sdkInt;
        public String serialNo = "";
        public final StructList<Display> displays = new StructList<>(Display::new);
        public final StructList<Encoder> encoders = new StructList<>(Encoder::new);
        public AudioStatus audioStatus = AudioStatus.AVAILABLE;
        public long capabilities;
        public boolean resumed;

        @Override public MessageType type() { return MessageType.WELCOME; }

        @Override
        public void visit(FieldVisitor v) {
            serverVersion = v.str8("serverVersion", serverVersion, 32);
            manufacturer = v.str8("manufacturer", manufacturer, 64);
            model = v.str8("model", model, 64);
            androidRelease = v.str8("androidRelease", androidRelease, 32);
            sdkInt = v.u16("sdkInt", sdkInt);
            serialNo = v.str8("serialNo", serialNo, 64);
            v.list8("displays", displays, 0, 16);
            v.list8("encoders", encoders, 0, 32);
            audioStatus = v.enumeration("audioStatus", audioStatus, AudioStatus.CODEC);
            capabilities = v.u32Mask("capabilities", capabilities, Constants.CAPABILITY_MASK);
            resumed = v.bool("resumed", resumed);
        }
    }

    public static final class HandshakeError implements Message {
        public ErrorCode code = ErrorCode.NONE;
        public String message = "";

        @Override public MessageType type() { return MessageType.HANDSHAKE_ERROR; }

        @Override
        public void visit(FieldVisitor v) {
            code = v.enumeration("code", code, ErrorCode.CODEC);
            message = v.str16("message", message, 1024);
        }
    }

    public static final class ChannelOpen implements Message {
        public ChannelId channel = ChannelId.VIDEO;
        public byte[] proof = new byte[32];

        @Override public MessageType type() { return MessageType.CHANNEL_OPEN; }

        @Override
        public void visit(FieldVisitor v) {
            channel = v.enumeration("channel", channel, ChannelId.CODEC);
            proof = v.bytes("proof", proof, 32);
        }
    }

    public static final class ChannelAccepted implements Message {
        public ChannelId channel = ChannelId.VIDEO;

        @Override public MessageType type() { return MessageType.CHANNEL_ACCEPTED; }

        @Override
        public void visit(FieldVisitor v) {
            channel = v.enumeration("channel", channel, ChannelId.CODEC);
        }
    }

    public static final class TouchFrame implements Message {
        public static final int MAX_POINTS = Constants.MAX_POINTER_ID + 1;

        public int videoSession;
        public final StructList<TouchPoint> points = new StructList<>(TouchPoint::new);

        @Override public MessageType type() { return MessageType.TOUCH_FRAME; }

        @Override
        public void visit(FieldVisitor v) {
            videoSession = v.u16("videoSession", videoSession);
            v.list8("points", points, 1, MAX_POINTS);
        }

        @Override
        public boolean isValid() {
            int seen = 0;
            for (TouchPoint point : points) {
                if (point.pointerId < 0 || point.pointerId > Constants.MAX_POINTER_ID) {
                    return false;
                }
                final int bit = 1 << point.pointerId;
                if ((seen & bit) != 0) {
                    return false;
                }
                seen |= bit;
            }
            return true;
        }
    }

    public static final class Key implements Message {
        public KeyAction action = KeyAction.DOWN;
        public long keycode;
        public long metaState;
        public int repeat;

        @Override public MessageType type() { return MessageType.KEY; }

        @Override
        public void visit(FieldVisitor v) {
            action = v.enumeration("action", action, KeyAction.CODEC);
            keycode = v.u32("keycode", keycode);
            metaState = v.u32("metaState", metaState);
            repeat = v.u16("repeat", repeat);
        }
    }

    public static final class Text implements Message {
        public String text = "";

        @Override public MessageType type() { return MessageType.TEXT; }

        @Override
        public void visit(FieldVisitor v) {
            text = v.str16("text", text, 4096);
        }
    }

    public static final class Scroll implements Message {
        public int x;
        public int y;
        public int videoSession;
        public int hScroll;
        public int vScroll;

        @Override public MessageType type() { return MessageType.SCROLL; }

        @Override
        public void visit(FieldVisitor v) {
            x = v.u16("x", x);
            y = v.u16("y", y);
            videoSession = v.u16("videoSession", videoSession);
            hScroll = v.i16("hScroll", hScroll);
            vScroll = v.i16("vScroll", vScroll);
        }
    }

    public static final class BackOrScreenOn implements Message {
        public KeyAction action = KeyAction.DOWN;

        @Override public MessageType type() { return MessageType.BACK_OR_SCREEN_ON; }

        @Override
        public void visit(FieldVisitor v) {
            action = v.enumeration("action", action, KeyAction.CODEC);
        }
    }

    public static final class UhidCreate implements Message {
        public static final int BUS_USB = 0x03;
        public static final int BUS_BLUETOOTH = 0x05;

        public int id;
        public int vendorId;
        public int productId;
        public String name = "";
        public byte[] reportDescriptor = new byte[0];
        public int bus = BUS_USB;

        @Override public MessageType type() { return MessageType.UHID_CREATE; }

        @Override
        public void visit(FieldVisitor v) {
            id = v.u16("id", id);
            vendorId = v.u16("vendorId", vendorId);
            productId = v.u16("productId", productId);
            name = v.str8("name", name, 127);
            reportDescriptor = v.blob16("reportDescriptor", reportDescriptor, 4096);
            bus = v.u8("bus", bus);
        }
    }

    public static final class UhidInput implements Message {
        public int id;
        public byte[] report = new byte[0];

        @Override public MessageType type() { return MessageType.UHID_INPUT; }

        @Override
        public void visit(FieldVisitor v) {
            id = v.u16("id", id);
            report = v.blob16("report", report, 512);
        }
    }

    public static final class UhidDestroy implements Message {
        public int id;

        @Override public MessageType type() { return MessageType.UHID_DESTROY; }

        @Override
        public void visit(FieldVisitor v) {
            id = v.u16("id", id);
        }
    }

    public static final class UhidFeature implements Message {
        public int id;
        public int reportNumber;
        public byte[] data = new byte[0];

        @Override public MessageType type() { return MessageType.UHID_FEATURE; }

        @Override
        public void visit(FieldVisitor v) {
            id = v.u16("id", id);
            reportNumber = v.u8("reportNumber", reportNumber);
            data = v.blob16("data", data, 512);
        }
    }

    public static final class ConfigureVideo implements Message {
        public VideoSource source = VideoSource.DISPLAY;
        public long displayId;
        public int virtualWidth;
        public int virtualHeight;
        public int virtualDensity;
        public VideoCodec codec = VideoCodec.H264;
        public int maxSize;
        public int maxFps;
        public long bitrate;
        public int intraRefreshMs;

        @Override public MessageType type() { return MessageType.CONFIGURE_VIDEO; }

        @Override
        public void visit(FieldVisitor v) {
            source = v.enumeration("source", source, VideoSource.CODEC);
            displayId = v.u32("displayId", displayId);
            virtualWidth = v.u16("virtualWidth", virtualWidth);
            virtualHeight = v.u16("virtualHeight", virtualHeight);
            virtualDensity = v.u16("virtualDensity", virtualDensity);
            codec = v.enumeration("codec", codec, VideoCodec.CODEC);
            maxSize = v.u16("maxSize", maxSize);
            maxFps = v.u16("maxFps", maxFps);
            bitrate = v.u32("bitrate", bitrate);
            intraRefreshMs = v.u16("intraRefreshMs", intraRefreshMs);
        }
    }

    public static final class SetVideoBitrate implements Message {
        public long bitrate;

        @Override public MessageType type() { return MessageType.SET_VIDEO_BITRATE; }

        @Override
        public void visit(FieldVisitor v) {
            bitrate = v.u32("bitrate", bitrate);
        }
    }

    public static final class SetMaxFps implements Message {
        public int maxFps;

        @Override public MessageType type() { return MessageType.SET_MAX_FPS; }

        @Override
        public void visit(FieldVisitor v) {
            maxFps = v.u16("maxFps", maxFps);
        }
    }

    public static final class RequestKeyFrame implements Message {
        @Override public MessageType type() { return MessageType.REQUEST_KEY_FRAME; }

        @Override
        public void visit(FieldVisitor v) {
        }
    }

    public static final class SetVideoPaused implements Message {
        public boolean paused;

        @Override public MessageType type() { return MessageType.SET_VIDEO_PAUSED; }

        @Override
        public void visit(FieldVisitor v) {
            paused = v.bool("paused", paused);
        }
    }

    public static final class ConfigureAudio implements Message {
        public boolean enabled;
        public long bitrate;

        @Override public MessageType type() { return MessageType.CONFIGURE_AUDIO; }

        @Override
        public void visit(FieldVisitor v) {
            enabled = v.bool("enabled", enabled);
            bitrate = v.u32("bitrate", bitrate);
        }
    }

    public static final class SetClipboard implements Message {
        public long sequence;
        public boolean paste;
        public String text = "";

        @Override public MessageType type() { return MessageType.SET_CLIPBOARD; }

        @Override
        public void visit(FieldVisitor v) {
            sequence = v.u32("sequence", sequence);
            paste = v.bool("paste", paste);
            text = v.str32("text", text, 262_000);
        }
    }

    public static final class GetClipboard implements Message {
        public CopyKey copyKey = CopyKey.NONE;

        @Override public MessageType type() { return MessageType.GET_CLIPBOARD; }

        @Override
        public void visit(FieldVisitor v) {
            copyKey = v.enumeration("copyKey", copyKey, CopyKey.CODEC);
        }
    }

    public static final class SetDisplayPower implements Message {
        public boolean on;

        @Override public MessageType type() { return MessageType.SET_DISPLAY_POWER; }

        @Override
        public void visit(FieldVisitor v) {
            on = v.bool("on", on);
        }
    }

    public static final class SetOrientation implements Message {
        public OrientationMode mode = OrientationMode.FOLLOW;

        @Override public MessageType type() { return MessageType.SET_ORIENTATION; }

        @Override
        public void visit(FieldVisitor v) {
            mode = v.enumeration("mode", mode, OrientationMode.CODEC);
        }
    }

    public static final class StartApp implements Message {
        public static final int FORCE_STOP = 1;
        public static final int ON_VIDEO_DISPLAY = 2;

        public String packageName = "";
        public int flags;

        @Override public MessageType type() { return MessageType.START_APP; }

        @Override
        public void visit(FieldVisitor v) {
            packageName = v.str8("package", packageName, 255);
            flags = v.flags8(flags, "forceStop", "onVideoDisplay");
        }
    }

    public static final class SystemPanel implements Message {
        public SystemPanelAction action = SystemPanelAction.EXPAND_NOTIFICATIONS;

        @Override public MessageType type() { return MessageType.SYSTEM_PANEL; }

        @Override
        public void visit(FieldVisitor v) {
            action = v.enumeration("action", action, SystemPanelAction.CODEC);
        }
    }

    public static final class RequestAppList implements Message {
        public long requestId;
        public boolean includeSystem;

        @Override public MessageType type() { return MessageType.REQUEST_APP_LIST; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            includeSystem = v.bool("includeSystem", includeSystem);
        }
    }

    public static final class RequestAppIcon implements Message {
        public long requestId;
        public String packageName = "";
        public int sizePx = 16;

        @Override public MessageType type() { return MessageType.REQUEST_APP_ICON; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            packageName = v.str8("package", packageName, 255);
            sizePx = v.u16Range("sizePx", sizePx, 16, 512);
        }
    }

    public static final class RequestScreenshot implements Message {
        public long requestId;
        public long displayId;
        public ScreenshotFormat format = ScreenshotFormat.PNG;

        @Override public MessageType type() { return MessageType.REQUEST_SCREENSHOT; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            displayId = v.u32("displayId", displayId);
            format = v.enumeration("format", format, ScreenshotFormat.CODEC);
        }
    }

    public static final class SetShowTouches implements Message {
        public boolean on;

        @Override public MessageType type() { return MessageType.SET_SHOW_TOUCHES; }

        @Override
        public void visit(FieldVisitor v) {
            on = v.bool("on", on);
        }
    }

    public static final class SubscribeStatus implements Message {
        public static final int MIN_PERIOD_MS = 500;
        public static final int MAX_PERIOD_MS = 60_000;

        public boolean on;
        public int periodMs = MIN_PERIOD_MS;

        @Override public MessageType type() { return MessageType.SUBSCRIBE_STATUS; }

        @Override
        public void visit(FieldVisitor v) {
            on = v.bool("on", on);
            periodMs = v.u16Range("periodMs", periodMs, MIN_PERIOD_MS, MAX_PERIOD_MS);
        }
    }

    public static final class Ping implements Message {
        public long nonce;
        public long clientTimeUs;

        @Override public MessageType type() { return MessageType.PING; }

        @Override
        public void visit(FieldVisitor v) {
            nonce = v.u32("nonce", nonce);
            clientTimeUs = v.u64("clientTimeUs", clientTimeUs);
        }
    }

    public static final class Goodbye implements Message {
        public ErrorCode reason = ErrorCode.NONE;

        @Override public MessageType type() { return MessageType.GOODBYE; }

        @Override
        public void visit(FieldVisitor v) {
            reason = v.enumeration("reason", reason, ErrorCode.CODEC);
        }
    }

    public static final class ReceiverReport implements Message {
        public int videoSession;
        public long intervalUs;
        public int frames;
        public long bytes;
        public long delayMinUs;
        public long delayMeanUs;
        public long delayMaxUs;
        public long roundTripUs;
        public long lostPackets;
        public long recoveredPackets;

        @Override public MessageType type() { return MessageType.RECEIVER_REPORT; }

        @Override
        public void visit(FieldVisitor v) {
            videoSession = v.u16("videoSession", videoSession);
            intervalUs = v.u32("intervalUs", intervalUs);
            frames = v.u16("frames", frames);
            bytes = v.u32("bytes", bytes);
            delayMinUs = v.i64("delayMinUs", delayMinUs);
            delayMeanUs = v.i64("delayMeanUs", delayMeanUs);
            delayMaxUs = v.i64("delayMaxUs", delayMaxUs);
            roundTripUs = v.u32("roundTripUs", roundTripUs);
            lostPackets = v.u32("lostPackets", lostPackets);
            recoveredPackets = v.u32("recoveredPackets", recoveredPackets);
        }
    }

    public static final class UhidOutput implements Message {
        public int id;
        public byte[] data = new byte[0];

        @Override public MessageType type() { return MessageType.UHID_OUTPUT; }

        @Override
        public void visit(FieldVisitor v) {
            id = v.u16("id", id);
            data = v.blob16("data", data, 512);
        }
    }

    public static final class UhidStatus implements Message {
        public int id;
        public boolean recognized;
        public boolean vibration;

        @Override public MessageType type() { return MessageType.UHID_STATUS; }

        @Override
        public void visit(FieldVisitor v) {
            id = v.u16("id", id);
            recognized = v.bool("recognized", recognized);
            vibration = v.bool("vibration", vibration);
        }
    }

    public static final class ClipboardChanged implements Message {
        public String text = "";

        @Override public MessageType type() { return MessageType.CLIPBOARD_CHANGED; }

        @Override
        public void visit(FieldVisitor v) {
            text = v.str32("text", text, 262_000);
        }
    }

    public static final class ClipboardAck implements Message {
        public long sequence;

        @Override public MessageType type() { return MessageType.CLIPBOARD_ACK; }

        @Override
        public void visit(FieldVisitor v) {
            sequence = v.u32("sequence", sequence);
        }
    }

    public static final class DisplayChanged implements Message {
        public final Display display = new Display();

        @Override public MessageType type() { return MessageType.DISPLAY_CHANGED; }

        @Override
        public void visit(FieldVisitor v) {
            v.object("display", display);
        }
    }

    public static final class ForegroundAppChanged implements Message {
        public static final int GAME = 1;

        public String packageName = "";
        public String activity = "";
        public int flags;

        @Override public MessageType type() { return MessageType.FOREGROUND_APP_CHANGED; }

        @Override
        public void visit(FieldVisitor v) {
            packageName = v.str8("package", packageName, 255);
            activity = v.str8("activity", activity, 255);
            flags = v.flags8(flags, "game");
        }
    }

    public static final class DeviceStatus implements Message {
        public static final int MAX_BATTERY = 100;
        public static final int UNKNOWN_BATTERY = 255;
        public static final int UNKNOWN_TEMPERATURE = Short.MIN_VALUE;
        public static final int MAX_CPU = 1000;
        public static final int UNKNOWN_CPU = 65_535;

        public int batteryPercent = UNKNOWN_BATTERY;
        public boolean charging;
        public int temperatureDeciC = UNKNOWN_TEMPERATURE;
        public int cpuPermille = UNKNOWN_CPU;

        @Override public MessageType type() { return MessageType.DEVICE_STATUS; }

        @Override
        public void visit(FieldVisitor v) {
            batteryPercent = v.u8("batteryPercent", batteryPercent);
            charging = v.bool("charging", charging);
            temperatureDeciC = v.i16("temperatureDeciC", temperatureDeciC);
            cpuPermille = v.u16("cpuPermille", cpuPermille);
        }

        @Override
        public boolean isValid() {
            return (batteryPercent <= MAX_BATTERY || batteryPercent == UNKNOWN_BATTERY)
                    && (cpuPermille <= MAX_CPU || cpuPermille == UNKNOWN_CPU);
        }
    }

    public static final class Pong implements Message {
        public long nonce;
        public long clientTimeUs;
        public long serverTimeUs;

        @Override public MessageType type() { return MessageType.PONG; }

        @Override
        public void visit(FieldVisitor v) {
            nonce = v.u32("nonce", nonce);
            clientTimeUs = v.u64("clientTimeUs", clientTimeUs);
            serverTimeUs = v.u64("serverTimeUs", serverTimeUs);
        }
    }

    public static final class ServerGoodbye implements Message {
        public ErrorCode reason = ErrorCode.NONE;
        public String message = "";

        @Override public MessageType type() { return MessageType.SERVER_GOODBYE; }

        @Override
        public void visit(FieldVisitor v) {
            reason = v.enumeration("reason", reason, ErrorCode.CODEC);
            message = v.str16("message", message, 1024);
        }
    }

    public static final class Notice implements Message {
        public NoticeLevel level = NoticeLevel.INFO;
        public ErrorCode code = ErrorCode.NONE;
        public String message = "";

        @Override public MessageType type() { return MessageType.NOTICE; }

        @Override
        public void visit(FieldVisitor v) {
            level = v.enumeration("level", level, NoticeLevel.CODEC);
            code = v.enumeration("code", code, ErrorCode.CODEC);
            message = v.str16("message", message, 1024);
        }
    }

    public static final class AppList implements Message {
        public long requestId;
        public final StructList<AppEntry> apps = new StructList<>(AppEntry::new);

        @Override public MessageType type() { return MessageType.APP_LIST; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            v.list16("apps", apps, 0, 4096);
        }
    }

    public static final class AppIcon implements Message {
        public long requestId;
        public String packageName = "";
        public byte[] png = new byte[0];

        @Override public MessageType type() { return MessageType.APP_ICON; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            packageName = v.str8("package", packageName, 255);
            png = v.blob32("png", png, 1024 * 1024);
        }
    }

    public static final class Screenshot implements Message {
        public long requestId;
        public int width;
        public int height;
        public ScreenshotFormat format = ScreenshotFormat.PNG;
        public byte[] data = new byte[0];

        @Override public MessageType type() { return MessageType.SCREENSHOT; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            width = v.u16("width", width);
            height = v.u16("height", height);
            format = v.enumeration("format", format, ScreenshotFormat.CODEC);
            data = v.blob32("data", data, 48 * 1024 * 1024);
        }
    }

    public static final class RequestFailed implements Message {
        public long requestId;
        public ErrorCode code = ErrorCode.NONE;
        public String message = "";

        @Override public MessageType type() { return MessageType.REQUEST_FAILED; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            code = v.enumeration("code", code, ErrorCode.CODEC);
            message = v.str16("message", message, 1024);
        }
    }

    public static final class CompanionHello implements Message {
        public int version = Constants.COMPANION_VERSION;
        public byte[] token = new byte[0];

        @Override public MessageType type() { return MessageType.COMPANION_HELLO; }

        @Override
        public void visit(FieldVisitor v) {
            version = v.u16("version", version);
            token = v.blob8("token", token, Constants.COMPANION_TOKEN_SIZE);
        }
    }

    public static final class CursorState implements Message {
        public static final int VISIBLE = 1;
        public static final int PRESSED = 2;

        public int x;
        public int y;
        public int flags;

        @Override public MessageType type() { return MessageType.CURSOR_STATE; }

        @Override
        public void visit(FieldVisitor v) {
            x = v.u16("x", x);
            y = v.u16("y", y);
            flags = v.flags8(flags, "visible", "pressed");
        }

        public boolean visible() {
            return (flags & VISIBLE) != 0;
        }

        public boolean pressed() {
            return (flags & PRESSED) != 0;
        }
    }

    public static final class CompanionWelcome implements Message {
        public int version = Constants.COMPANION_VERSION;
        public OverlayStatus overlay = OverlayStatus.GRANTED;
        public int width;
        public int height;
        public int rotation;

        @Override public MessageType type() { return MessageType.COMPANION_WELCOME; }

        @Override
        public void visit(FieldVisitor v) {
            version = v.u16("version", version);
            overlay = v.enumeration("overlay", overlay, OverlayStatus.CODEC);
            width = v.u16("width", width);
            height = v.u16("height", height);
            rotation = v.u8Range("rotation", rotation, 0, 3);
        }
    }

    public static final class OverlayChanged implements Message {
        public OverlayStatus overlay = OverlayStatus.GRANTED;
        public int width;
        public int height;
        public int rotation;

        @Override public MessageType type() { return MessageType.OVERLAY_CHANGED; }

        @Override
        public void visit(FieldVisitor v) {
            overlay = v.enumeration("overlay", overlay, OverlayStatus.CODEC);
            width = v.u16("width", width);
            height = v.u16("height", height);
            rotation = v.u8Range("rotation", rotation, 0, 3);
        }
    }

    public static final class CommitText implements Message {
        public String text = "";

        @Override public MessageType type() { return MessageType.COMMIT_TEXT; }

        @Override
        public void visit(FieldVisitor v) {
            text = v.str16("text", text, Constants.COMPANION_TEXT_LIMIT);
        }
    }

    public static final class EditorAction implements Message {
        public ImeAction action = ImeAction.UNSPECIFIED;

        @Override public MessageType type() { return MessageType.EDITOR_ACTION; }

        @Override
        public void visit(FieldVisitor v) {
            action = v.enumeration("action", action, ImeAction.CODEC);
        }
    }

    public static final class KeyHint implements Struct {
        public KeyHintShape shape = KeyHintShape.TAP;
        public int x;
        public int y;
        public int width;
        public int height;
        public String label = "";

        @Override
        public void visit(FieldVisitor v) {
            shape = v.enumeration("shape", shape, KeyHintShape.CODEC);
            x = v.u16("x", x);
            y = v.u16("y", y);
            width = v.u16("width", width);
            height = v.u16("height", height);
            label = v.str8("label", label, Constants.MAX_KEY_HINT_LABEL);
        }
    }

    public static final class KeyHintLayout implements Message {
        public final StructList<KeyHint> hints = new StructList<>(KeyHint::new);

        @Override public MessageType type() { return MessageType.KEY_HINT_LAYOUT; }

        @Override
        public void visit(FieldVisitor v) {
            v.list8("hints", hints, 0, Constants.MAX_KEY_HINTS);
        }
    }

    public static final class KeyHintState implements Message {
        public static final int SHOWN = 1;
        public static final int FADED = 2;

        public int flags;
        public int opacity = 100;

        @Override public MessageType type() { return MessageType.KEY_HINT_STATE; }

        @Override
        public void visit(FieldVisitor v) {
            flags = v.flags8(flags, "shown", "faded");
            opacity = v.u8Range("opacity", opacity, 0, 100);
        }

        public boolean shown() {
            return (flags & SHOWN) != 0;
        }

        public boolean faded() {
            return (flags & FADED) != 0;
        }
    }

    public static final class InputBoxChanged implements Message {
        public static final int SHOWN = 1;
        public static final int MULTI_LINE = 2;

        public int flags;
        public long inputType;
        public ImeAction action = ImeAction.UNSPECIFIED;

        @Override public MessageType type() { return MessageType.INPUT_BOX_CHANGED; }

        @Override
        public void visit(FieldVisitor v) {
            flags = v.flags8(flags, "shown", "multiLine");
            inputType = v.u32("inputType", inputType);
            action = v.enumeration("action", action, ImeAction.CODEC);
        }

        public boolean shown() {
            return (flags & SHOWN) != 0;
        }

        public boolean multiLine() {
            return (flags & MULTI_LINE) != 0;
        }
    }

    public static final class ScreenshotRequest implements Message {
        public long requestId;
        public int maxSide;
        public int quality = Constants.DEFAULT_SCREENSHOT_QUALITY;

        @Override public MessageType type() { return MessageType.SCREENSHOT_REQUEST; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            maxSide = v.u16("maxSide", maxSide);
            quality = v.u8Range("quality", quality, 1, 100);
        }
    }

    public static final class ScreenshotChunk implements Message {
        public long requestId;
        public int index;
        public int total = 1;
        public byte[] data = new byte[0];

        @Override public MessageType type() { return MessageType.SCREENSHOT_CHUNK; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            index = v.u16("index", index);
            total = v.u16Range("total", total, 1, Constants.MAX_SCREENSHOT_CHUNKS);
            data = v.blob16("data", data, Constants.MAX_SCREENSHOT_CHUNK);
        }

        @Override
        public boolean isValid() {
            return index < total;
        }
    }

    public static final class ScreenshotFailed implements Message {
        public long requestId;
        public ScreenshotProblem reason = ScreenshotProblem.ERROR;
        public String message = "";

        @Override public MessageType type() { return MessageType.SCREENSHOT_FAILED; }

        @Override
        public void visit(FieldVisitor v) {
            requestId = v.u32("requestId", requestId);
            reason = v.enumeration("reason", reason, ScreenshotProblem.CODEC);
            message = v.str16("message", message, 1024);
        }
    }
}
