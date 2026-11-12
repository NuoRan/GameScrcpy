package com.gamescrcpy.protocol;

import java.util.Arrays;
import java.util.Comparator;
import java.util.function.Supplier;

public enum MessageType {
    CLIENT_HELLO(0x0002, "ClientHello", Messages.ClientHello::new),
    CHANNEL_OPEN(0x0005, "ChannelOpen", Messages.ChannelOpen::new),

    TOUCH_FRAME(0x0101, "TouchFrame", Messages.TouchFrame::new),
    KEY(0x0102, "Key", Messages.Key::new),
    TEXT(0x0103, "Text", Messages.Text::new),
    SCROLL(0x0104, "Scroll", Messages.Scroll::new),
    BACK_OR_SCREEN_ON(0x0105, "BackOrScreenOn", Messages.BackOrScreenOn::new),
    UHID_CREATE(0x0110, "UhidCreate", Messages.UhidCreate::new),
    UHID_INPUT(0x0111, "UhidInput", Messages.UhidInput::new),
    UHID_DESTROY(0x0112, "UhidDestroy", Messages.UhidDestroy::new),
    UHID_FEATURE(0x0113, "UhidFeature", Messages.UhidFeature::new),

    CONFIGURE_VIDEO(0x0201, "ConfigureVideo", Messages.ConfigureVideo::new),
    SET_VIDEO_BITRATE(0x0202, "SetVideoBitrate", Messages.SetVideoBitrate::new),
    SET_MAX_FPS(0x0203, "SetMaxFps", Messages.SetMaxFps::new),
    REQUEST_KEY_FRAME(0x0204, "RequestKeyFrame", Messages.RequestKeyFrame::new),
    SET_VIDEO_PAUSED(0x0205, "SetVideoPaused", Messages.SetVideoPaused::new),
    CONFIGURE_AUDIO(0x0211, "ConfigureAudio", Messages.ConfigureAudio::new),

    SET_CLIPBOARD(0x0301, "SetClipboard", Messages.SetClipboard::new),
    GET_CLIPBOARD(0x0302, "GetClipboard", Messages.GetClipboard::new),
    SET_DISPLAY_POWER(0x0303, "SetDisplayPower", Messages.SetDisplayPower::new),
    SET_ORIENTATION(0x0304, "SetOrientation", Messages.SetOrientation::new),
    START_APP(0x0305, "StartApp", Messages.StartApp::new),
    SYSTEM_PANEL(0x0306, "SystemPanel", Messages.SystemPanel::new),
    REQUEST_APP_LIST(0x0307, "RequestAppList", Messages.RequestAppList::new),
    REQUEST_APP_ICON(0x0308, "RequestAppIcon", Messages.RequestAppIcon::new),
    REQUEST_SCREENSHOT(0x0309, "RequestScreenshot", Messages.RequestScreenshot::new),
    SET_SHOW_TOUCHES(0x030A, "SetShowTouches", Messages.SetShowTouches::new),
    SUBSCRIBE_STATUS(0x030B, "SubscribeStatus", Messages.SubscribeStatus::new),

    PING(0x0401, "Ping", Messages.Ping::new),
    GOODBYE(0x0402, "Goodbye", Messages.Goodbye::new),
    RECEIVER_REPORT(0x0403, "ReceiverReport", Messages.ReceiverReport::new),

    COMPANION_HELLO(0x0601, "CompanionHello", Messages.CompanionHello::new),
    CURSOR_STATE(0x0602, "CursorState", Messages.CursorState::new),
    COMMIT_TEXT(0x0603, "CommitText", Messages.CommitText::new),
    EDITOR_ACTION(0x0604, "EditorAction", Messages.EditorAction::new),
    KEY_HINT_LAYOUT(0x0605, "KeyHintLayout", Messages.KeyHintLayout::new),
    KEY_HINT_STATE(0x0606, "KeyHintState", Messages.KeyHintState::new),
    SCREENSHOT_REQUEST(0x0607, "ScreenshotRequest", Messages.ScreenshotRequest::new),
    COMPANION_SETTINGS(0x0608, "CompanionSettings", Messages.CompanionSettings::new),

    SERVER_HELLO(0x8001, "ServerHello", Messages.ServerHello::new),
    WELCOME(0x8003, "Welcome", Messages.Welcome::new),
    HANDSHAKE_ERROR(0x8004, "HandshakeError", Messages.HandshakeError::new),
    CHANNEL_ACCEPTED(0x8006, "ChannelAccepted", Messages.ChannelAccepted::new),

    UHID_OUTPUT(0x8110, "UhidOutput", Messages.UhidOutput::new),
    UHID_STATUS(0x8111, "UhidStatus", Messages.UhidStatus::new),
    CLIPBOARD_CHANGED(0x8301, "ClipboardChanged", Messages.ClipboardChanged::new),
    CLIPBOARD_ACK(0x8302, "ClipboardAck", Messages.ClipboardAck::new),
    DISPLAY_CHANGED(0x8303, "DisplayChanged", Messages.DisplayChanged::new),
    FOREGROUND_APP_CHANGED(0x8304, "ForegroundAppChanged", Messages.ForegroundAppChanged::new),
    DEVICE_STATUS(0x8305, "DeviceStatus", Messages.DeviceStatus::new),
    PONG(0x8401, "Pong", Messages.Pong::new),
    SERVER_GOODBYE(0x8402, "ServerGoodbye", Messages.ServerGoodbye::new),
    NOTICE(0x8403, "Notice", Messages.Notice::new),

    APP_LIST(0x8501, "AppList", Messages.AppList::new),
    APP_ICON(0x8502, "AppIcon", Messages.AppIcon::new),
    SCREENSHOT(0x8503, "Screenshot", Messages.Screenshot::new),
    REQUEST_FAILED(0x8504, "RequestFailed", Messages.RequestFailed::new),

    COMPANION_WELCOME(0x8601, "CompanionWelcome", Messages.CompanionWelcome::new),
    OVERLAY_CHANGED(0x8602, "OverlayChanged", Messages.OverlayChanged::new),
    INPUT_BOX_CHANGED(0x8603, "InputBoxChanged", Messages.InputBoxChanged::new),
    SCREENSHOT_CHUNK(0x8604, "ScreenshotChunk", Messages.ScreenshotChunk::new),
    SCREENSHOT_FAILED(0x8605, "ScreenshotFailed", Messages.ScreenshotFailed::new),
    COMPANION_SETTING_CHANGE(0x8606, "CompanionSettingChange", Messages.CompanionSettingChange::new);

    private static final MessageType[] BY_CODE = Arrays.stream(values())
            .sorted(Comparator.comparingInt(MessageType::code))
            .toArray(MessageType[]::new);
    private static final int[] CODES = Arrays.stream(BY_CODE).mapToInt(MessageType::code).toArray();

    private final int code;
    private final String specName;
    private final Supplier<Message> factory;

    MessageType(int code, String specName, Supplier<Message> factory) {
        this.code = code;
        this.specName = specName;
        this.factory = factory;
    }

    public int code() {
        return code;
    }

    public String specName() {
        return specName;
    }

    public Message create() {
        return factory.get();
    }

    public Direction direction() {
        return (code & 0x8000) != 0 ? Direction.TO_CLIENT : Direction.TO_SERVER;
    }

    public FrameContext context() {
        final int range = (code >>> 8) & 0x7F;
        if (range == 0x00) {
            return FrameContext.HANDSHAKE;
        }
        if (range == 0x06) {
            return FrameContext.COMPANION;
        }
        return range == 0x05 ? FrameContext.BULK : FrameContext.CONTROL;
    }

    public static MessageType fromCode(int code) {
        final int index = Arrays.binarySearch(CODES, code);
        return index >= 0 ? BY_CODE[index] : null;
    }
}
