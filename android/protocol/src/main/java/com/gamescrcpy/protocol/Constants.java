package com.gamescrcpy.protocol;

public final class Constants {

    public static final int PROTOCOL_VERSION = 1;
    public static final long MAGIC = 0x47534350L;

    public static final int FRAME_HEADER_SIZE = 6;
    public static final int MEDIA_HEADER_SIZE = 20;

    public static final int HANDSHAKE_PAYLOAD_LIMIT = 16 * 1024;
    public static final int CONTROL_PAYLOAD_LIMIT = 256 * 1024;
    public static final int BULK_PAYLOAD_LIMIT = 64 * 1024 * 1024;
    public static final int COMPANION_PAYLOAD_LIMIT = 16 * 1024;
    public static final int VIDEO_PAYLOAD_LIMIT = 16 * 1024 * 1024;
    public static final int AUDIO_PAYLOAD_LIMIT = 64 * 1024;

    public static final int MAX_POINTER_ID = 31;
    public static final int COMPANION_VERSION = 1;
    public static final int COMPANION_TOKEN_SIZE = 16;
    public static final int COMPANION_TEXT_LIMIT = 4096;
    public static final int MAX_KEY_HINTS = 192;
    public static final int MAX_KEY_HINT_LABEL = 32;
    public static final int MAX_SCREENSHOT_CHUNK = 15 * 1024;
    public static final int MAX_SCREENSHOT_CHUNKS = 274;
    public static final int MAX_SCREENSHOT_BYTES = 4 * 1024 * 1024;
    public static final int DEFAULT_SCREENSHOT_SIDE = 1920;
    public static final int DEFAULT_SCREENSHOT_QUALITY = 90;
    public static final long CAPABILITY_MASK = 0x1FFFL;

    public static final long CAPABILITY_CLIPBOARD = 1L << 0;
    public static final long CAPABILITY_TEXT = 1L << 1;
    public static final long CAPABILITY_UHID = 1L << 2;
    public static final long CAPABILITY_DISPLAY_POWER = 1L << 3;
    public static final long CAPABILITY_ROTATION = 1L << 4;
    public static final long CAPABILITY_APPS = 1L << 5;
    public static final long CAPABILITY_SCREENSHOT = 1L << 6;
    public static final long CAPABILITY_FOREGROUND_APP = 1L << 7;
    public static final long CAPABILITY_VIRTUAL_DISPLAY = 1L << 8;
    public static final long CAPABILITY_SHOW_TOUCHES = 1L << 9;
    public static final long CAPABILITY_INDEPENDENT_DISPLAY = 1L << 10;
    public static final long CAPABILITY_DEVICE_STATUS = 1L << 11;
    public static final long CAPABILITY_UHID_GAMEPAD = 1L << 12;

    private Constants() {
    }
}
