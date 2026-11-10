package com.gamescrcpy.protocol;

public final class Enums {

    private Enums() {
    }

    public enum TouchAction implements WireEnum {
        DOWN(0, "down"), UP(1, "up"), MOVE(2, "move"), CANCEL(3, "cancel");

        public static final EnumCodec<TouchAction> CODEC = EnumCodec.u8(TouchAction.class);
        private final int value;
        private final String name;

        TouchAction(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum KeyAction implements WireEnum {
        DOWN(0, "down"), UP(1, "up");

        public static final EnumCodec<KeyAction> CODEC = EnumCodec.u8(KeyAction.class);
        private final int value;
        private final String name;

        KeyAction(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum VideoSource implements WireEnum {
        DISPLAY(0, "display"), VIRTUAL_DISPLAY(1, "virtualDisplay");

        public static final EnumCodec<VideoSource> CODEC = EnumCodec.u8(VideoSource.class);
        private final int value;
        private final String name;

        VideoSource(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum VideoCodec implements WireEnum {
        H264(1, "h264"), H265(2, "h265"), AV1(3, "av1");

        public static final EnumCodec<VideoCodec> CODEC = EnumCodec.u8(VideoCodec.class);
        private final int value;
        private final String name;

        VideoCodec(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum AudioCodec implements WireEnum {
        OPUS(1, "opus");

        public static final EnumCodec<AudioCodec> CODEC = EnumCodec.u8(AudioCodec.class);
        private final int value;
        private final String name;

        AudioCodec(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum CopyKey implements WireEnum {
        NONE(0, "none"), COPY(1, "copy"), CUT(2, "cut");

        public static final EnumCodec<CopyKey> CODEC = EnumCodec.u8(CopyKey.class);
        private final int value;
        private final String name;

        CopyKey(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum OrientationMode implements WireEnum {
        FOLLOW(0, "follow"), LOCK_0(1, "lock0"), LOCK_90(2, "lock90"), LOCK_180(3, "lock180"),
        LOCK_270(4, "lock270");

        public static final EnumCodec<OrientationMode> CODEC = EnumCodec.u8(OrientationMode.class);
        private final int value;
        private final String name;

        OrientationMode(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum SystemPanelAction implements WireEnum {
        EXPAND_NOTIFICATIONS(0, "expandNotifications"), EXPAND_QUICK_SETTINGS(1, "expandQuickSettings"),
        COLLAPSE(2, "collapse");

        public static final EnumCodec<SystemPanelAction> CODEC = EnumCodec.u8(SystemPanelAction.class);
        private final int value;
        private final String name;

        SystemPanelAction(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum NoticeLevel implements WireEnum {
        INFO(0, "info"), WARNING(1, "warning"), ERROR(2, "error");

        public static final EnumCodec<NoticeLevel> CODEC = EnumCodec.u8(NoticeLevel.class);
        private final int value;
        private final String name;

        NoticeLevel(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum AudioStatus implements WireEnum {
        AVAILABLE(0, "available"), UNSUPPORTED_ANDROID(1, "unsupportedAndroid"),
        CAPTURE_FAILED(2, "captureFailed");

        public static final EnumCodec<AudioStatus> CODEC = EnumCodec.u8(AudioStatus.class);
        private final int value;
        private final String name;

        AudioStatus(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum ChannelId implements WireEnum {
        VIDEO(1, "video"), AUDIO(2, "audio"), BULK(3, "bulk");

        public static final EnumCodec<ChannelId> CODEC = EnumCodec.u8(ChannelId.class);
        private final int value;
        private final String name;

        ChannelId(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum ScreenshotFormat implements WireEnum {
        PNG(0, "png");

        public static final EnumCodec<ScreenshotFormat> CODEC = EnumCodec.u8(ScreenshotFormat.class);
        private final int value;
        private final String name;

        ScreenshotFormat(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum MediaKind implements WireEnum {
        SESSION(0, "session"), CONFIG(1, "config"), FRAME(2, "frame");

        public static final EnumCodec<MediaKind> CODEC = EnumCodec.u8(MediaKind.class);
        private final int value;
        private final String name;

        MediaKind(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum OverlayStatus implements WireEnum {
        GRANTED(0, "granted"), NO_PERMISSION(1, "noPermission");

        public static final EnumCodec<OverlayStatus> CODEC = EnumCodec.u8(OverlayStatus.class);
        private final int value;
        private final String name;

        OverlayStatus(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum ImeAction implements WireEnum {
        UNSPECIFIED(0, "unspecified"), NONE(1, "none"), GO(2, "go"), SEARCH(3, "search"), SEND(4, "send"),
        NEXT(5, "next"), DONE(6, "done"), PREVIOUS(7, "previous");

        public static final EnumCodec<ImeAction> CODEC = EnumCodec.u8(ImeAction.class);
        private final int value;
        private final String name;

        ImeAction(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum KeyHintShape implements WireEnum {
        TAP(0, "tap"), HOLD(1, "hold"), LONG_PRESS(2, "longPress"), TURBO(3, "turbo"), CAMERA(4, "camera"),
        AIM(5, "aim"), FREE_LOOK(6, "freeLook"), SWIPE(7, "swipe"), KEY(8, "key"), RING(9, "ring"),
        UNBOUND(10, "unbound");

        public static final EnumCodec<KeyHintShape> CODEC = EnumCodec.u8(KeyHintShape.class);
        private final int value;
        private final String name;

        KeyHintShape(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum ScreenshotProblem implements WireEnum {
        REFUSED(0, "refused"), UNSUPPORTED(1, "unsupported"), BUSY(2, "busy"), ERROR(3, "error");

        public static final EnumCodec<ScreenshotProblem> CODEC = EnumCodec.u8(ScreenshotProblem.class);
        private final int value;
        private final String name;

        ScreenshotProblem(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }

    public enum ErrorCode implements WireEnum {
        NONE(0, "none"), VERSION_MISMATCH(1, "versionMismatch"),
        AUTHENTICATION_FAILED(2, "authenticationFailed"), UNSUPPORTED_ANDROID(3, "unsupportedAndroid"),
        MALFORMED_FRAME(4, "malformedFrame"), FRAME_TOO_LARGE(5, "frameTooLarge"),
        UNKNOWN_TYPE(6, "unknownType"), WRONG_DIRECTION(7, "wrongDirection"),
        UNEXPECTED_MESSAGE(8, "unexpectedMessage"), CHANNEL_UNAVAILABLE(9, "channelUnavailable"),
        RESOURCE_BUSY(10, "resourceBusy"), ENCODER_FAILURE(11, "encoderFailure"),
        PERMISSION_DENIED(12, "permissionDenied"), NOT_FOUND(13, "notFound"), TIMEOUT(14, "timeout"),
        INTERNAL_ERROR(15, "internalError"), INJECTION_DENIED(16, "injectionDenied"),
        CLIPBOARD_DENIED(17, "clipboardDenied"), AUDIO_CAPTURE_FAILED(18, "audioCaptureFailed"),
        VIDEO_REDUCED(19, "videoReduced"), VIDEO_RESTORED(20, "videoRestored");

        public static final EnumCodec<ErrorCode> CODEC = EnumCodec.u16(ErrorCode.class);
        private final int value;
        private final String name;

        ErrorCode(int value, String name) {
            this.value = value;
            this.name = name;
        }

        @Override public int wireValue() { return value; }
        @Override public String specName() { return name; }
    }
}
