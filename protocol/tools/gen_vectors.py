#!/usr/bin/env python3
"""Reference encoder for GameScrcpy protocol v1 (protocol/SPEC.md).

An independent implementation written directly from the specification (ADR-0026). It generates
the golden vectors in protocol/vectors/, which the C++ and Java codecs must reproduce byte for
byte. Standard library only; the output is deterministic.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import struct
from pathlib import Path
from typing import Callable

PROTOCOL_VERSION = 1
MAGIC = 0x47534350

HANDSHAKE_MAX = 16 * 1024
CONTROL_MAX = 256 * 1024
BULK_MAX = 64 * 1024 * 1024
COMPANION_MAX = 16 * 1024
VIDEO_MAX = 16 * 1024 * 1024
AUDIO_MAX = 64 * 1024
STATUS_PERIOD_MIN = 500
STATUS_PERIOD_MAX = 60000
UNKNOWN_BATTERY = 255
UNKNOWN_TEMPERATURE = -32768
UNKNOWN_CPU = 65535

TOUCH_ACTION = ["down", "up", "move", "cancel"]
KEY_ACTION = ["down", "up"]
VIDEO_SOURCE = ["display", "virtualDisplay"]
VIDEO_CODEC = {"h264": 1, "h265": 2, "av1": 3}
AUDIO_CODEC = {"opus": 1}
COPY_KEY = ["none", "copy", "cut"]
ORIENTATION = ["follow", "lock0", "lock90", "lock180", "lock270"]
PANEL_ACTION = ["expandNotifications", "expandQuickSettings", "collapse"]
NOTICE_LEVEL = ["info", "warning", "error"]
AUDIO_STATUS = ["available", "unsupportedAndroid", "captureFailed"]
CHANNEL = {"video": 1, "audio": 2, "bulk": 3}
SCREENSHOT_FORMAT = ["png"]
OVERLAY_STATUS = ["granted", "noPermission"]
IME_ACTION = ["unspecified", "none", "go", "search", "send", "next", "done", "previous"]
KEY_HINT_SHAPE = ["tap", "hold", "longPress", "turbo", "camera", "aim", "freeLook", "swipe", "key", "ring",
                  "unbound"]
MAX_KEY_HINTS = 192
SCREENSHOT_PROBLEM = ["refused", "unsupported", "busy", "error"]
SHOT_CHUNK_MAX = 15 * 1024
SHOT_CHUNKS_MAX = 274
SHOT_TOTAL_MAX = 4 * 1024 * 1024
assert (SHOT_CHUNKS_MAX - 1) * SHOT_CHUNK_MAX < SHOT_TOTAL_MAX <= SHOT_CHUNKS_MAX * SHOT_CHUNK_MAX
COMPUTER_LINK = ["usb", "wireless", "lan"]
COMPANION_ITEM = ["cursor", "hints", "hintOpacity", "ime", "autoConnect"]
COMPUTER_NAME_MAX = 64
PHONE_HINT_OPACITY_MIN = 10
PHONE_HINT_OPACITY_MAX = 100
ACCENT_MAX = 0xFFFFFF
ERROR_CODE = [
    "none", "versionMismatch", "authenticationFailed", "unsupportedAndroid", "malformedFrame",
    "frameTooLarge", "unknownType", "wrongDirection", "unexpectedMessage", "channelUnavailable",
    "resourceBusy", "encoderFailure", "permissionDenied", "notFound", "timeout", "internalError",
    "injectionDenied", "clipboardDenied", "audioCaptureFailed", "videoReduced", "videoRestored",
]


def enum_value(table: list[str] | dict[str, int], name: str) -> int:
    return table[name] if isinstance(table, dict) else table.index(name)


class Writer:
    def __init__(self) -> None:
        self.data = bytearray()

    def u8(self, value: int) -> None:
        self.data += struct.pack(">B", value)

    def u16(self, value: int) -> None:
        self.data += struct.pack(">H", value)

    def u32(self, value: int) -> None:
        self.data += struct.pack(">I", value)

    def u64(self, value: int) -> None:
        self.data += struct.pack(">Q", value)

    def i16(self, value: int) -> None:
        self.data += struct.pack(">h", value)

    def i64(self, value: int) -> None:
        self.data += struct.pack(">q", value)

    def boolean(self, value: bool) -> None:
        self.u8(1 if value else 0)

    def fixed(self, hex_text: str, size: int) -> None:
        data = bytes.fromhex(hex_text)
        assert len(data) == size
        self.data += data

    def string(self, prefix: str, text: str, maximum: int) -> None:
        data = text.encode("utf-8")
        assert len(data) <= maximum, (text[:16], maximum)
        self._length(prefix, len(data))
        self.data += data

    def blob(self, prefix: str, hex_text: str, maximum: int) -> None:
        data = bytes.fromhex(hex_text)
        assert len(data) <= maximum
        self._length(prefix, len(data))
        self.data += data

    def _length(self, prefix: str, length: int) -> None:
        {"u8": self.u8, "u16": self.u16, "u32": self.u32}[prefix](length)


MESSAGES: dict[str, tuple[int, str, Callable[[Writer, dict], None]]] = {}


def message(name: str, type_id: int, channel: str):
    def register(encode: Callable[[Writer, dict], None]):
        MESSAGES[name] = (type_id, channel, encode)
        return encode

    return register


def encode_display(w: Writer, d: dict) -> None:
    assert 0 <= d["rotation"] <= 3
    w.u32(d["displayId"])
    w.u16(d["width"])
    w.u16(d["height"])
    w.u8(d["rotation"])
    w.u16(d["densityDpi"])
    w.u32(d["refreshMilliHz"])
    for side in ("safeLeft", "safeTop", "safeRight", "safeBottom"):
        w.u16(d[side])


def encode_encoder(w: Writer, e: dict) -> None:
    w.u8(VIDEO_CODEC[e["codec"]])
    w.string("u8", e["name"], 128)
    w.boolean(e["hardware"])


@message("ServerHello", 0x8001, "handshake")
def _server_hello(w: Writer, f: dict) -> None:
    w.u32(MAGIC)
    w.u16(f["protocolVersion"])
    w.fixed(f["nonce"], 16)


@message("ClientHello", 0x0002, "handshake")
def _client_hello(w: Writer, f: dict) -> None:
    w.u16(f["protocolVersion"])
    w.string("u8", f["clientVersion"], 32)
    w.fixed(f["proof"], 32)


@message("Welcome", 0x8003, "handshake")
def _welcome(w: Writer, f: dict) -> None:
    w.string("u8", f["serverVersion"], 32)
    w.string("u8", f["manufacturer"], 64)
    w.string("u8", f["model"], 64)
    w.string("u8", f["androidRelease"], 32)
    w.u16(f["sdkInt"])
    w.string("u8", f["serialNo"], 64)
    assert len(f["displays"]) <= 16 and len(f["encoders"]) <= 32
    w.u8(len(f["displays"]))
    for d in f["displays"]:
        encode_display(w, d)
    w.u8(len(f["encoders"]))
    for e in f["encoders"]:
        encode_encoder(w, e)
    w.u8(enum_value(AUDIO_STATUS, f["audioStatus"]))
    assert f["capabilities"] < (1 << 13)
    w.u32(f["capabilities"])
    w.boolean(f["resumed"])


@message("HandshakeError", 0x8004, "handshake")
def _handshake_error(w: Writer, f: dict) -> None:
    w.u16(enum_value(ERROR_CODE, f["code"]))
    w.string("u16", f["message"], 1024)


@message("ChannelOpen", 0x0005, "handshake")
def _channel_open(w: Writer, f: dict) -> None:
    w.u8(CHANNEL[f["channel"]])
    w.fixed(f["proof"], 32)


@message("ChannelAccepted", 0x8006, "handshake")
def _channel_accepted(w: Writer, f: dict) -> None:
    w.u8(CHANNEL[f["channel"]])


@message("TouchFrame", 0x0101, "control")
def _touch_frame(w: Writer, f: dict) -> None:
    points = f["points"]
    ids = [p["pointerId"] for p in points]
    assert 1 <= len(points) <= 32 and len(set(ids)) == len(ids)
    w.u16(f["videoSession"])
    w.u8(len(points))
    for p in points:
        assert 0 <= p["pointerId"] <= 31
        w.u8(enum_value(TOUCH_ACTION, p["action"]))
        w.u8(p["pointerId"])
        w.u16(p["x"])
        w.u16(p["y"])
        w.u16(p["pressure"])


@message("Key", 0x0102, "control")
def _key(w: Writer, f: dict) -> None:
    w.u8(enum_value(KEY_ACTION, f["action"]))
    w.u32(f["keycode"])
    w.u32(f["metaState"])
    w.u16(f["repeat"])


@message("Text", 0x0103, "control")
def _text(w: Writer, f: dict) -> None:
    w.string("u16", f["text"], 4096)


@message("Scroll", 0x0104, "control")
def _scroll(w: Writer, f: dict) -> None:
    w.u16(f["x"])
    w.u16(f["y"])
    w.u16(f["videoSession"])
    w.i16(f["hScroll"])
    w.i16(f["vScroll"])


@message("BackOrScreenOn", 0x0105, "control")
def _back_or_screen_on(w: Writer, f: dict) -> None:
    w.u8(enum_value(KEY_ACTION, f["action"]))


@message("UhidCreate", 0x0110, "control")
def _uhid_create(w: Writer, f: dict) -> None:
    w.u16(f["id"])
    w.u16(f["vendorId"])
    w.u16(f["productId"])
    w.string("u8", f["name"], 127)
    w.blob("u16", f["reportDescriptor"], 4096)
    w.u8(f["bus"])


@message("UhidInput", 0x0111, "control")
def _uhid_input(w: Writer, f: dict) -> None:
    w.u16(f["id"])
    w.blob("u16", f["report"], 512)


@message("UhidDestroy", 0x0112, "control")
def _uhid_destroy(w: Writer, f: dict) -> None:
    w.u16(f["id"])


@message("UhidFeature", 0x0113, "control")
def _uhid_feature(w: Writer, f: dict) -> None:
    w.u16(f["id"])
    w.u8(f["reportNumber"])
    w.blob("u16", f["data"], 512)


@message("ConfigureVideo", 0x0201, "control")
def _configure_video(w: Writer, f: dict) -> None:
    w.u8(enum_value(VIDEO_SOURCE, f["source"]))
    w.u32(f["displayId"])
    w.u16(f["virtualWidth"])
    w.u16(f["virtualHeight"])
    w.u16(f["virtualDensity"])
    w.u8(VIDEO_CODEC[f["codec"]])
    w.u16(f["maxSize"])
    w.u16(f["maxFps"])
    w.u32(f["bitrate"])
    w.u16(f["intraRefreshMs"])


@message("SetVideoBitrate", 0x0202, "control")
def _set_video_bitrate(w: Writer, f: dict) -> None:
    w.u32(f["bitrate"])


@message("SetMaxFps", 0x0203, "control")
def _set_max_fps(w: Writer, f: dict) -> None:
    w.u16(f["maxFps"])


@message("RequestKeyFrame", 0x0204, "control")
def _request_key_frame(w: Writer, f: dict) -> None:
    del w, f


@message("SetVideoPaused", 0x0205, "control")
def _set_video_paused(w: Writer, f: dict) -> None:
    w.boolean(f["paused"])


@message("ConfigureAudio", 0x0211, "control")
def _configure_audio(w: Writer, f: dict) -> None:
    w.boolean(f["enabled"])
    w.u32(f["bitrate"])


@message("SetClipboard", 0x0301, "control")
def _set_clipboard(w: Writer, f: dict) -> None:
    w.u32(f["sequence"])
    w.boolean(f["paste"])
    w.string("u32", f["text"], 262000)


@message("GetClipboard", 0x0302, "control")
def _get_clipboard(w: Writer, f: dict) -> None:
    w.u8(enum_value(COPY_KEY, f["copyKey"]))


@message("SetDisplayPower", 0x0303, "control")
def _set_display_power(w: Writer, f: dict) -> None:
    w.boolean(f["on"])


@message("SetOrientation", 0x0304, "control")
def _set_orientation(w: Writer, f: dict) -> None:
    w.u8(enum_value(ORIENTATION, f["mode"]))


@message("StartApp", 0x0305, "control")
def _start_app(w: Writer, f: dict) -> None:
    w.string("u8", f["package"], 255)
    w.u8((1 if f["forceStop"] else 0) | (2 if f["onVideoDisplay"] else 0))


@message("SystemPanel", 0x0306, "control")
def _system_panel(w: Writer, f: dict) -> None:
    w.u8(enum_value(PANEL_ACTION, f["action"]))


@message("RequestAppList", 0x0307, "control")
def _request_app_list(w: Writer, f: dict) -> None:
    w.u32(f["requestId"])
    w.boolean(f["includeSystem"])


@message("RequestAppIcon", 0x0308, "control")
def _request_app_icon(w: Writer, f: dict) -> None:
    assert 16 <= f["sizePx"] <= 512
    w.u32(f["requestId"])
    w.string("u8", f["package"], 255)
    w.u16(f["sizePx"])


@message("RequestScreenshot", 0x0309, "control")
def _request_screenshot(w: Writer, f: dict) -> None:
    w.u32(f["requestId"])
    w.u32(f["displayId"])
    w.u8(enum_value(SCREENSHOT_FORMAT, f["format"]))


@message("SetShowTouches", 0x030A, "control")
def _set_show_touches(w: Writer, f: dict) -> None:
    w.boolean(f["on"])


@message("SubscribeStatus", 0x030B, "control")
def _subscribe_status(w: Writer, f: dict) -> None:
    assert STATUS_PERIOD_MIN <= f["periodMs"] <= STATUS_PERIOD_MAX
    w.boolean(f["on"])
    w.u16(f["periodMs"])


@message("Ping", 0x0401, "control")
def _ping(w: Writer, f: dict) -> None:
    w.u32(f["nonce"])
    w.u64(int(f["clientTimeUs"]))


@message("Goodbye", 0x0402, "control")
def _goodbye(w: Writer, f: dict) -> None:
    w.u16(enum_value(ERROR_CODE, f["reason"]))


@message("ReceiverReport", 0x0403, "control")
def _receiver_report(w: Writer, f: dict) -> None:
    w.u16(f["videoSession"])
    w.u32(f["intervalUs"])
    w.u16(f["frames"])
    w.u32(f["bytes"])
    w.i64(int(f["delayMinUs"]))
    w.i64(int(f["delayMeanUs"]))
    w.i64(int(f["delayMaxUs"]))
    w.u32(f["roundTripUs"])
    w.u32(f["lostPackets"])
    w.u32(f["recoveredPackets"])


@message("UhidOutput", 0x8110, "control")
def _uhid_output(w: Writer, f: dict) -> None:
    w.u16(f["id"])
    w.blob("u16", f["data"], 512)


@message("UhidStatus", 0x8111, "control")
def _uhid_status(w: Writer, f: dict) -> None:
    w.u16(f["id"])
    w.boolean(f["recognized"])
    w.boolean(f["vibration"])


@message("ClipboardChanged", 0x8301, "control")
def _clipboard_changed(w: Writer, f: dict) -> None:
    w.string("u32", f["text"], 262000)


@message("ClipboardAck", 0x8302, "control")
def _clipboard_ack(w: Writer, f: dict) -> None:
    w.u32(f["sequence"])


@message("DisplayChanged", 0x8303, "control")
def _display_changed(w: Writer, f: dict) -> None:
    encode_display(w, f["display"])


@message("ForegroundAppChanged", 0x8304, "control")
def _foreground_app_changed(w: Writer, f: dict) -> None:
    w.string("u8", f["package"], 255)
    w.string("u8", f["activity"], 255)
    w.u8(1 if f["game"] else 0)


@message("DeviceStatus", 0x8305, "control")
def _device_status(w: Writer, f: dict) -> None:
    assert 0 <= f["batteryPercent"] <= 100 or f["batteryPercent"] == UNKNOWN_BATTERY
    assert 0 <= f["cpuPermille"] <= 1000 or f["cpuPermille"] == UNKNOWN_CPU
    w.u8(f["batteryPercent"])
    w.boolean(f["charging"])
    w.i16(f["temperatureDeciC"])
    w.u16(f["cpuPermille"])


@message("Pong", 0x8401, "control")
def _pong(w: Writer, f: dict) -> None:
    w.u32(f["nonce"])
    w.u64(int(f["clientTimeUs"]))
    w.u64(int(f["serverTimeUs"]))


@message("ServerGoodbye", 0x8402, "control")
def _server_goodbye(w: Writer, f: dict) -> None:
    w.u16(enum_value(ERROR_CODE, f["reason"]))
    w.string("u16", f["message"], 1024)


@message("Notice", 0x8403, "control")
def _notice(w: Writer, f: dict) -> None:
    w.u8(enum_value(NOTICE_LEVEL, f["level"]))
    w.u16(enum_value(ERROR_CODE, f["code"]))
    w.string("u16", f["message"], 1024)


@message("AppList", 0x8501, "bulk")
def _app_list(w: Writer, f: dict) -> None:
    w.u32(f["requestId"])
    assert len(f["apps"]) <= 4096
    w.u16(len(f["apps"]))
    for app in f["apps"]:
        w.string("u8", app["package"], 255)
        w.string("u8", app["label"], 255)
        w.u64(int(app["versionCode"]))
        w.u8((1 if app["system"] else 0) | (2 if app["enabled"] else 0) | (4 if app["game"] else 0))


@message("AppIcon", 0x8502, "bulk")
def _app_icon(w: Writer, f: dict) -> None:
    w.u32(f["requestId"])
    w.string("u8", f["package"], 255)
    w.blob("u32", f["png"], 1024 * 1024)


@message("Screenshot", 0x8503, "bulk")
def _screenshot(w: Writer, f: dict) -> None:
    w.u32(f["requestId"])
    w.u16(f["width"])
    w.u16(f["height"])
    w.u8(enum_value(SCREENSHOT_FORMAT, f["format"]))
    w.blob("u32", f["data"], 48 * 1024 * 1024)


@message("RequestFailed", 0x8504, "bulk")
def _request_failed(w: Writer, f: dict) -> None:
    w.u32(f["requestId"])
    w.u16(enum_value(ERROR_CODE, f["code"]))
    w.string("u16", f["message"], 1024)


def encode_overlay(w: Writer, f: dict) -> None:
    assert 0 <= f["rotation"] <= 3
    w.u8(enum_value(OVERLAY_STATUS, f["overlay"]))
    w.u16(f["width"])
    w.u16(f["height"])
    w.u8(f["rotation"])


@message("CompanionHello", 0x0601, "companion")
def _companion_hello(w: Writer, f: dict) -> None:
    w.u16(f["version"])
    w.blob("u8", f["token"], 16)


@message("CursorState", 0x0602, "companion")
def _cursor_state(w: Writer, f: dict) -> None:
    w.u16(f["x"])
    w.u16(f["y"])
    w.u8((1 if f["visible"] else 0) | (2 if f["pressed"] else 0))


@message("CompanionWelcome", 0x8601, "companion")
def _companion_welcome(w: Writer, f: dict) -> None:
    w.u16(f["version"])
    encode_overlay(w, f)


@message("OverlayChanged", 0x8602, "companion")
def _overlay_changed(w: Writer, f: dict) -> None:
    encode_overlay(w, f)


@message("CommitText", 0x0603, "companion")
def _commit_text(w: Writer, f: dict) -> None:
    w.string("u16", f["text"], 4096)


@message("EditorAction", 0x0604, "companion")
def _editor_action(w: Writer, f: dict) -> None:
    w.u8(enum_value(IME_ACTION, f["action"]))


@message("KeyHintLayout", 0x0605, "companion")
def _key_hint_layout(w: Writer, f: dict) -> None:
    hints = f["hints"]
    assert len(hints) <= MAX_KEY_HINTS
    w.u8(len(hints))
    for hint in hints:
        w.u8(enum_value(KEY_HINT_SHAPE, hint["shape"]))
        w.u16(hint["x"])
        w.u16(hint["y"])
        w.u16(hint["width"])
        w.u16(hint["height"])
        w.string("u8", hint["label"], 32)


@message("KeyHintState", 0x0606, "companion")
def _key_hint_state(w: Writer, f: dict) -> None:
    assert 0 <= f["opacity"] <= 100
    w.u8((1 if f["shown"] else 0) | (2 if f["faded"] else 0))
    w.u8(f["opacity"])


@message("InputBoxChanged", 0x8603, "companion")
def _input_box_changed(w: Writer, f: dict) -> None:
    w.u8((1 if f["shown"] else 0) | (2 if f["multiLine"] else 0))
    w.u32(f["inputType"])
    w.u8(enum_value(IME_ACTION, f["action"]))


@message("ScreenshotRequest", 0x0607, "companion")
def _screenshot_request(w: Writer, f: dict) -> None:
    assert 1 <= f["quality"] <= 100
    w.u32(f["requestId"])
    w.u16(f["maxSide"])
    w.u8(f["quality"])


@message("ScreenshotChunk", 0x8604, "companion")
def _screenshot_chunk(w: Writer, f: dict) -> None:
    assert 1 <= f["total"] <= SHOT_CHUNKS_MAX and 0 <= f["index"] < f["total"]
    w.u32(f["requestId"])
    w.u16(f["index"])
    w.u16(f["total"])
    w.blob("u16", f["data"], SHOT_CHUNK_MAX)


@message("ScreenshotFailed", 0x8605, "companion")
def _screenshot_failed(w: Writer, f: dict) -> None:
    w.u32(f["requestId"])
    w.u8(enum_value(SCREENSHOT_PROBLEM, f["reason"]))
    w.string("u16", f["message"], 1024)


@message("CompanionSettings", 0x0608, "companion")
def _companion_settings(w: Writer, f: dict) -> None:
    assert 0 <= f["accent"] <= ACCENT_MAX
    assert PHONE_HINT_OPACITY_MIN <= f["hintOpacity"] <= PHONE_HINT_OPACITY_MAX
    w.fixed(f["computer"], 16)
    w.string("u8", f["name"], COMPUTER_NAME_MAX)
    w.u8(enum_value(COMPUTER_LINK, f["link"]))
    w.u32(f["accent"])
    w.u32(f["applied"])
    w.boolean(f["cursor"])
    w.boolean(f["hints"])
    w.u8(f["hintOpacity"])
    w.boolean(f["ime"])
    w.boolean(f["autoConnect"])


def companion_item_range(item: str) -> tuple[int, int]:
    if item == "hintOpacity":
        return PHONE_HINT_OPACITY_MIN, PHONE_HINT_OPACITY_MAX
    return 0, 1


@message("CompanionSettingChange", 0x8606, "companion")
def _companion_setting_change(w: Writer, f: dict) -> None:
    low, high = companion_item_range(f["item"])
    assert f["change"] >= 1 and low <= f["value"] <= high
    w.u32(f["change"])
    w.u8(enum_value(COMPANION_ITEM, f["item"]))
    w.u8(f["value"])


def frame_limit(channel: str) -> int:
    return {"handshake": HANDSHAKE_MAX, "control": CONTROL_MAX, "bulk": BULK_MAX,
            "companion": COMPANION_MAX}[channel]


def companion_allows(type_id: int) -> bool:
    return ((type_id >> 8) & 0x7F) == 0x06 or (type_id & 0x7FFF) in (0x0401, 0x0402)


def raw_frame(type_id: int, payload: bytes, length: int | None = None) -> bytes:
    return struct.pack(">HI", type_id, len(payload) if length is None else length) + payload


def encode_frame(name: str, fields: dict) -> bytes:
    type_id, channel, encode = MESSAGES[name]
    w = Writer()
    encode(w, fields)
    assert len(w.data) <= frame_limit(channel)
    return raw_frame(type_id, bytes(w.data))


def media_header(kind: int, flags: int, session: int, pts: int, size: int, encode_delay: int = 0) -> bytes:
    return struct.pack(">BBHqII", kind, flags, session, pts, encode_delay, size)


def encode_media(name: str, fields: dict) -> bytes:
    session = fields["session"]
    assert 1 <= session <= 0xFFFF
    if name == "VideoSession":
        assert 0 <= fields["rotation"] <= 3
        payload = struct.pack(
            ">BHHIHHB", VIDEO_CODEC[fields["codec"]], fields["frameWidth"], fields["frameHeight"],
            fields["displayId"], fields["displayWidth"], fields["displayHeight"],
            fields["rotation"])
        return media_header(0, 0, session, 0, len(payload)) + payload
    if name == "AudioSession":
        payload = struct.pack(">BIB", AUDIO_CODEC[fields["codec"]], fields["sampleRate"],
                              fields["channels"])
        return media_header(0, 0, session, 0, len(payload)) + payload
    payload = bytes.fromhex(fields["payload"])
    if name == "MediaConfig":
        return media_header(1, 0, session, 0, len(payload)) + payload
    assert name == "MediaFrame"
    flags = (1 if fields["keyFrame"] else 0) | (2 if fields["discardable"] else 0)
    assert 0 <= fields["encodeDelayUs"] <= 0xFFFFFFFF
    return media_header(2, flags, session, int(fields["ptsUs"]), len(payload), fields["encodeDelayUs"]) + payload


def direction_of(type_id: int) -> str:
    return "toClient" if type_id & 0x8000 else "toServer"


def ok(name: str, message_name: str, fields: dict, channel: str | None = None) -> dict:
    if message_name in MESSAGES:
        type_id, default_channel, _ = MESSAGES[message_name]
        data = encode_frame(message_name, fields)
        record_channel, direction = channel or default_channel, direction_of(type_id)
        assert record_channel == default_channel or (record_channel == "companion"
                                                     and companion_allows(type_id))
    else:
        assert channel in ("video", "audio")
        data = encode_media(message_name, fields)
        record_channel, direction = channel, "toClient"
    return {"name": name, "message": message_name, "channel": record_channel,
            "direction": direction, "fields": fields, "hex": data.hex()}


def bad(name: str, channel: str, direction: str, error: str, data: bytes) -> dict:
    return {"name": name, "channel": channel, "direction": direction, "error": error,
            "hex": data.hex()}


def pattern(size: int, start: int = 0) -> str:
    return bytes((start + i) & 0xFF for i in range(size)).hex()


DISPLAY_PHONE = {"displayId": 0, "width": 1080, "height": 2400, "rotation": 0,
                 "densityDpi": 440, "refreshMilliHz": 120000,
                 "safeLeft": 0, "safeTop": 110, "safeRight": 0, "safeBottom": 0}
DISPLAY_MAX = {"displayId": 0xFFFFFFFF, "width": 0xFFFF, "height": 0xFFFF, "rotation": 3,
               "densityDpi": 0xFFFF, "refreshMilliHz": 0xFFFFFFFF,
               "safeLeft": 0xFFFF, "safeTop": 0xFFFF, "safeRight": 0xFFFF, "safeBottom": 0xFFFF}


def touch_point(action: str, pointer_id: int, x: int, y: int, pressure: int) -> dict:
    return {"action": action, "pointerId": pointer_id, "x": x, "y": y, "pressure": pressure}


def handshake_vectors() -> list[dict]:
    return [
        ok("server-hello-typical", "ServerHello",
           {"protocolVersion": 1, "nonce": pattern(16)}),
        ok("server-hello-zero-nonce", "ServerHello",
           {"protocolVersion": 1, "nonce": "00" * 16}),
        ok("server-hello-max-version", "ServerHello",
           {"protocolVersion": 0xFFFF, "nonce": "ff" * 16}),
        ok("client-hello-typical", "ClientHello",
           {"protocolVersion": 1, "clientVersion": "2.0.0", "proof": pattern(32, 0xA0)}),
        ok("client-hello-empty-version", "ClientHello",
           {"protocolVersion": 1, "clientVersion": "", "proof": "00" * 32}),
        ok("client-hello-max-version-string", "ClientHello",
           {"protocolVersion": 1, "clientVersion": "v" * 32, "proof": "ff" * 32}),
        ok("welcome-typical", "Welcome", {
            "serverVersion": "2.0.0", "manufacturer": "Xiaomi", "model": "23049RAD8C",
            "androidRelease": "14", "sdkInt": 34, "serialNo": "ca411890",
            "displays": [DISPLAY_PHONE],
            "encoders": [
                {"codec": "h264", "name": "c2.qti.avc.encoder", "hardware": True},
                {"codec": "h265", "name": "c2.qti.hevc.encoder", "hardware": True},
            ],
            "audioStatus": "available", "capabilities": 0x1FF, "resumed": False}),
        ok("welcome-minimal", "Welcome", {
            "serverVersion": "", "manufacturer": "", "model": "", "androidRelease": "9",
            "sdkInt": 28, "serialNo": "", "displays": [], "encoders": [],
            "audioStatus": "unsupportedAndroid", "capabilities": 0, "resumed": False}),
        ok("welcome-maximal", "Welcome", {
            "serverVersion": "s" * 32, "manufacturer": "m" * 64, "model": "游戏手机" * 5 + "x" * 4,
            "androidRelease": "r" * 32, "sdkInt": 0xFFFF, "serialNo": "0" * 64,
            "displays": [dict(DISPLAY_MAX, displayId=i) for i in range(16)],
            "encoders": [{"codec": ("h264", "h265", "av1")[i % 3], "name": "e" * 128,
                          "hardware": i % 2 == 0} for i in range(32)],
            "audioStatus": "captureFailed", "capabilities": 0x1FFF, "resumed": True}),
        ok("handshake-error-version", "HandshakeError",
           {"code": "versionMismatch", "message": "client 1, server 2"}),
        ok("handshake-error-auth-empty", "HandshakeError",
           {"code": "authenticationFailed", "message": ""}),
        ok("handshake-error-max-message", "HandshakeError",
           {"code": "internalError", "message": "x" * 1024}),
        ok("channel-open-video", "ChannelOpen", {"channel": "video", "proof": pattern(32, 0x10)}),
        ok("channel-open-audio", "ChannelOpen", {"channel": "audio", "proof": "00" * 32}),
        ok("channel-open-bulk", "ChannelOpen", {"channel": "bulk", "proof": "ff" * 32}),
        ok("channel-accepted-video", "ChannelAccepted", {"channel": "video"}),
        ok("channel-accepted-audio", "ChannelAccepted", {"channel": "audio"}),
        ok("channel-accepted-bulk", "ChannelAccepted", {"channel": "bulk"}),
    ]


def input_vectors() -> list[dict]:
    return [
        ok("touch-frame-single-down", "TouchFrame", {
            "videoSession": 1, "points": [touch_point("down", 0, 32768, 32768, 65535)]}),
        ok("touch-frame-joystick-and-camera", "TouchFrame", {
            "videoSession": 2, "points": [touch_point("move", 0, 9000, 48000, 65535),
                                          touch_point("move", 1, 41000, 20000, 65535)]}),
        ok("touch-frame-mixed-actions", "TouchFrame", {
            "videoSession": 3, "points": [touch_point("up", 1, 100, 200, 0),
                                          touch_point("move", 2, 300, 400, 30000),
                                          touch_point("down", 3, 500, 600, 65535),
                                          touch_point("cancel", 7, 0, 0, 1)]}),
        ok("touch-frame-max", "TouchFrame", {
            "videoSession": 65535,
            "points": [touch_point("move", i, 65535, 65535, 0) for i in range(32)]}),
        ok("key-down-w", "Key", {"action": "down", "keycode": 51, "metaState": 0, "repeat": 0}),
        ok("key-up-alt", "Key", {"action": "up", "keycode": 51, "metaState": 0x02, "repeat": 0}),
        ok("key-extremes", "Key", {"action": "down", "keycode": 0xFFFFFFFF,
                                   "metaState": 0xFFFFFFFF, "repeat": 0xFFFF}),
        ok("text-ascii", "Text", {"text": "hello"}),
        ok("text-cjk-emoji", "Text", {"text": "你好，世界🎮"}),
        ok("text-empty", "Text", {"text": ""}),
        ok("text-max", "Text", {"text": "a" * 4096}),
        ok("scroll-down", "Scroll", {"x": 32768, "y": 32768, "videoSession": 1, "hScroll": 0,
                                     "vScroll": -256}),
        ok("scroll-extremes", "Scroll", {"x": 65535, "y": 0, "videoSession": 65535,
                                         "hScroll": -32768, "vScroll": 32767}),
        ok("scroll-zero", "Scroll", {"x": 0, "y": 0, "videoSession": 1, "hScroll": 0,
                                     "vScroll": 0}),
        ok("back-down", "BackOrScreenOn", {"action": "down"}),
        ok("back-up", "BackOrScreenOn", {"action": "up"}),
        ok("uhid-create-typical", "UhidCreate", {
            "id": 1, "vendorId": 0x18D1, "productId": 0x4EE7, "name": "GameScrcpy Touch",
            "reportDescriptor": "05010902a101850109010a02a1008101c0c0", "bus": 3}),
        ok("uhid-create-empty", "UhidCreate", {"id": 0, "vendorId": 0, "productId": 0,
                                               "name": "", "reportDescriptor": "", "bus": 0}),
        ok("uhid-create-max", "UhidCreate", {"id": 0xFFFF, "vendorId": 0xFFFF,
                                             "productId": 0xFFFF, "name": "n" * 127,
                                             "reportDescriptor": pattern(4096), "bus": 0xFF}),
        ok("uhid-create-bluetooth-pad", "UhidCreate", {
            "id": 0x0100, "vendorId": 0x045E, "productId": 0x0B13, "name": "Xbox Wireless Controller",
            "reportDescriptor": "05010905a101850109300931c0", "bus": 5}),
        ok("uhid-feature-mac", "UhidFeature", {"id": 0x0101, "reportNumber": 0x81,
                                               "data": "8101005a534702"}),
        ok("uhid-feature-empty", "UhidFeature", {"id": 0, "reportNumber": 0, "data": ""}),
        ok("uhid-feature-max", "UhidFeature", {"id": 0xFFFF, "reportNumber": 0xFF,
                                               "data": pattern(512, 5)}),
        ok("uhid-input-typical", "UhidInput", {"id": 1, "report": "01000100100010"}),
        ok("uhid-input-empty", "UhidInput", {"id": 2, "report": ""}),
        ok("uhid-input-max", "UhidInput", {"id": 0xFFFF, "report": pattern(512, 3)}),
        ok("uhid-destroy-zero", "UhidDestroy", {"id": 0}),
        ok("uhid-destroy-one", "UhidDestroy", {"id": 1}),
        ok("uhid-destroy-max", "UhidDestroy", {"id": 0xFFFF}),
    ]


def media_config_vectors() -> list[dict]:
    return [
        ok("configure-video-display", "ConfigureVideo", {
            "source": "display", "displayId": 0, "virtualWidth": 0, "virtualHeight": 0,
            "virtualDensity": 0, "codec": "h264", "maxSize": 1920, "maxFps": 60,
            "bitrate": 8000000, "intraRefreshMs": 0}),
        ok("configure-video-virtual", "ConfigureVideo", {
            "source": "virtualDisplay", "displayId": 0, "virtualWidth": 1920,
            "virtualHeight": 1080, "virtualDensity": 320, "codec": "h265", "maxSize": 0,
            "maxFps": 0, "bitrate": 20000000, "intraRefreshMs": 2000}),
        ok("configure-video-extremes", "ConfigureVideo", {
            "source": "display", "displayId": 0xFFFFFFFF, "virtualWidth": 0xFFFF,
            "virtualHeight": 0xFFFF, "virtualDensity": 0xFFFF, "codec": "av1",
            "maxSize": 0xFFFF, "maxFps": 0xFFFF, "bitrate": 0xFFFFFFFF,
            "intraRefreshMs": 0xFFFF}),
        ok("set-video-bitrate-zero", "SetVideoBitrate", {"bitrate": 0}),
        ok("set-video-bitrate-typical", "SetVideoBitrate", {"bitrate": 8000000}),
        ok("set-video-bitrate-max", "SetVideoBitrate", {"bitrate": 0xFFFFFFFF}),
        ok("set-max-fps-unlimited", "SetMaxFps", {"maxFps": 0}),
        ok("set-max-fps-60", "SetMaxFps", {"maxFps": 60}),
        ok("set-max-fps-max", "SetMaxFps", {"maxFps": 0xFFFF}),
        ok("request-key-frame", "RequestKeyFrame", {}),
        ok("set-video-paused-true", "SetVideoPaused", {"paused": True}),
        ok("set-video-paused-false", "SetVideoPaused", {"paused": False}),
        ok("configure-audio-enabled", "ConfigureAudio", {"enabled": True, "bitrate": 128000}),
        ok("configure-audio-disabled", "ConfigureAudio", {"enabled": False, "bitrate": 0}),
        ok("configure-audio-max", "ConfigureAudio", {"enabled": True, "bitrate": 0xFFFFFFFF}),
    ]


def device_vectors() -> list[dict]:
    return [
        ok("set-clipboard-typical", "SetClipboard", {"sequence": 1, "paste": False,
                                                     "text": "copy me"}),
        ok("set-clipboard-cjk-paste", "SetClipboard", {"sequence": 0xFFFFFFFF, "paste": True,
                                                       "text": "复制粘贴 ✓"}),
        ok("set-clipboard-empty", "SetClipboard", {"sequence": 0, "paste": False, "text": ""}),
        ok("set-clipboard-large", "SetClipboard", {"sequence": 7, "paste": True,
                                                   "text": "剪贴板" * 333 + "x"}),
        ok("get-clipboard-none", "GetClipboard", {"copyKey": "none"}),
        ok("get-clipboard-copy", "GetClipboard", {"copyKey": "copy"}),
        ok("get-clipboard-cut", "GetClipboard", {"copyKey": "cut"}),
        ok("set-display-power-on", "SetDisplayPower", {"on": True}),
        ok("set-display-power-off", "SetDisplayPower", {"on": False}),
        ok("set-orientation-follow", "SetOrientation", {"mode": "follow"}),
        ok("set-orientation-lock90", "SetOrientation", {"mode": "lock90"}),
        ok("set-orientation-lock270", "SetOrientation", {"mode": "lock270"}),
        ok("start-app-typical", "StartApp", {"package": "com.example.game",
                                             "forceStop": False, "onVideoDisplay": False}),
        ok("start-app-force-stop", "StartApp", {"package": "com.tencent.tmgp.pubgmhd",
                                                "forceStop": True, "onVideoDisplay": False}),
        ok("start-app-video-display", "StartApp", {"package": "com.miHoYo.Yuanshen",
                                                   "forceStop": False, "onVideoDisplay": True}),
        ok("start-app-max", "StartApp", {"package": "p" * 255, "forceStop": True,
                                         "onVideoDisplay": True}),
        ok("system-panel-notifications", "SystemPanel", {"action": "expandNotifications"}),
        ok("system-panel-quick-settings", "SystemPanel", {"action": "expandQuickSettings"}),
        ok("system-panel-collapse", "SystemPanel", {"action": "collapse"}),
        ok("request-app-list-user", "RequestAppList", {"requestId": 1, "includeSystem": False}),
        ok("request-app-list-system", "RequestAppList", {"requestId": 2, "includeSystem": True}),
        ok("request-app-list-max-id", "RequestAppList", {"requestId": 0xFFFFFFFF,
                                                         "includeSystem": True}),
        ok("request-app-icon-typical", "RequestAppIcon", {"requestId": 1,
                                                          "package": "com.example.game",
                                                          "sizePx": 96}),
        ok("request-app-icon-min", "RequestAppIcon", {"requestId": 2, "package": "",
                                                      "sizePx": 16}),
        ok("request-app-icon-max", "RequestAppIcon", {"requestId": 0xFFFFFFFF,
                                                      "package": "p" * 255, "sizePx": 512}),
        ok("request-screenshot-typical", "RequestScreenshot", {"requestId": 1, "displayId": 0,
                                                               "format": "png"}),
        ok("request-screenshot-virtual", "RequestScreenshot", {"requestId": 9, "displayId": 2,
                                                               "format": "png"}),
        ok("request-screenshot-max", "RequestScreenshot", {"requestId": 0xFFFFFFFF,
                                                           "displayId": 0xFFFFFFFF,
                                                           "format": "png"}),
        ok("set-show-touches-on", "SetShowTouches", {"on": True}),
        ok("set-show-touches-off", "SetShowTouches", {"on": False}),
        ok("subscribe-status-on", "SubscribeStatus", {"on": True, "periodMs": 2000}),
        ok("subscribe-status-off", "SubscribeStatus", {"on": False, "periodMs": 2000}),
        ok("subscribe-status-fastest", "SubscribeStatus", {"on": True, "periodMs": STATUS_PERIOD_MIN}),
        ok("subscribe-status-slowest", "SubscribeStatus", {"on": True, "periodMs": STATUS_PERIOD_MAX}),
    ]


def maintenance_vectors() -> list[dict]:
    return [
        ok("ping-typical", "Ping", {"nonce": 1, "clientTimeUs": "1000"}),
        ok("ping-zero", "Ping", {"nonce": 0, "clientTimeUs": "0"}),
        ok("ping-max", "Ping", {"nonce": 0xFFFFFFFF, "clientTimeUs": str(2**64 - 1)}),
        ok("goodbye-none", "Goodbye", {"reason": "none"}),
        ok("goodbye-timeout", "Goodbye", {"reason": "timeout"}),
        ok("goodbye-internal", "Goodbye", {"reason": "internalError"}),
        ok("receiver-report-typical", "ReceiverReport", {
            "videoSession": 3, "intervalUs": 200_000, "frames": 12, "bytes": 96_240,
            "delayMinUs": "-1234567890123", "delayMeanUs": "-1234567885000", "delayMaxUs": "-1234567871000",
            "roundTripUs": 1_200, "lostPackets": 0, "recoveredPackets": 0}),
        ok("receiver-report-idle", "ReceiverReport", {
            "videoSession": 1, "intervalUs": 200_000, "frames": 0, "bytes": 0,
            "delayMinUs": "0", "delayMeanUs": "0", "delayMaxUs": "0",
            "roundTripUs": 0, "lostPackets": 0, "recoveredPackets": 0}),
        ok("receiver-report-max", "ReceiverReport", {
            "videoSession": 0xFFFF, "intervalUs": 0xFFFFFFFF, "frames": 0xFFFF, "bytes": 0xFFFFFFFF,
            "delayMinUs": str(-2**63), "delayMeanUs": "0", "delayMaxUs": str(2**63 - 1),
            "roundTripUs": 0xFFFFFFFF, "lostPackets": 0xFFFFFFFF, "recoveredPackets": 0xFFFFFFFF}),
    ]


def event_vectors() -> list[dict]:
    return [
        ok("uhid-output-typical", "UhidOutput", {"id": 1, "data": "02"}),
        ok("uhid-output-empty", "UhidOutput", {"id": 0, "data": ""}),
        ok("uhid-output-max", "UhidOutput", {"id": 0xFFFF, "data": pattern(512, 9)}),
        ok("uhid-status-recognized", "UhidStatus", {"id": 0x0101, "recognized": True, "vibration": True}),
        ok("uhid-status-not-recognized", "UhidStatus", {"id": 0x0102, "recognized": False,
                                                        "vibration": False}),
        ok("uhid-status-silent-pad", "UhidStatus", {"id": 0xFFFF, "recognized": True, "vibration": False}),
        ok("clipboard-changed-ascii", "ClipboardChanged", {"text": "hello"}),
        ok("clipboard-changed-cjk", "ClipboardChanged", {"text": "来自手机的文字"}),
        ok("clipboard-changed-empty", "ClipboardChanged", {"text": ""}),
        ok("clipboard-ack-zero", "ClipboardAck", {"sequence": 0}),
        ok("clipboard-ack-one", "ClipboardAck", {"sequence": 1}),
        ok("clipboard-ack-max", "ClipboardAck", {"sequence": 0xFFFFFFFF}),
        ok("display-changed-phone", "DisplayChanged", {"display": DISPLAY_PHONE}),
        ok("display-changed-rotated", "DisplayChanged", {"display": dict(
            DISPLAY_PHONE, width=2400, height=1080, rotation=1, safeLeft=110, safeTop=0)}),
        ok("display-changed-max", "DisplayChanged", {"display": DISPLAY_MAX}),
        ok("foreground-app-game", "ForegroundAppChanged", {
            "package": "com.tencent.tmgp.pubgmhd",
            "activity": "com.epicgames.ue4.GameActivity", "game": True}),
        ok("foreground-app-not-game", "ForegroundAppChanged", {
            "package": "com.android.settings",
            "activity": "com.android.settings.Settings", "game": False}),
        ok("foreground-app-empty", "ForegroundAppChanged", {"package": "", "activity": "",
                                                            "game": False}),
        ok("foreground-app-max", "ForegroundAppChanged", {"package": "p" * 255,
                                                          "activity": "a" * 255, "game": True}),
        ok("device-status-charging", "DeviceStatus", {"batteryPercent": 82, "charging": True,
                                                      "temperatureDeciC": 365, "cpuPermille": 230}),
        ok("device-status-unknown", "DeviceStatus", {"batteryPercent": UNKNOWN_BATTERY, "charging": False,
                                                     "temperatureDeciC": UNKNOWN_TEMPERATURE,
                                                     "cpuPermille": UNKNOWN_CPU}),
        ok("device-status-partly-known", "DeviceStatus", {"batteryPercent": 64, "charging": False,
                                                          "temperatureDeciC": UNKNOWN_TEMPERATURE,
                                                          "cpuPermille": 47}),
        ok("device-status-cold-empty-idle", "DeviceStatus", {"batteryPercent": 0, "charging": False,
                                                             "temperatureDeciC": -125, "cpuPermille": 0}),
        ok("device-status-extremes", "DeviceStatus", {"batteryPercent": 100, "charging": True,
                                                      "temperatureDeciC": 32767, "cpuPermille": 1000}),
        ok("pong-typical", "Pong", {"nonce": 1, "clientTimeUs": "1000",
                                    "serverTimeUs": "123456789"}),
        ok("pong-zero", "Pong", {"nonce": 0, "clientTimeUs": "0", "serverTimeUs": "0"}),
        ok("pong-max", "Pong", {"nonce": 0xFFFFFFFF, "clientTimeUs": str(2**64 - 1),
                                "serverTimeUs": str(2**64 - 1)}),
        ok("server-goodbye-timeout", "ServerGoodbye", {"reason": "timeout",
                                                       "message": "heartbeat timeout"}),
        ok("server-goodbye-empty", "ServerGoodbye", {"reason": "none", "message": ""}),
        ok("server-goodbye-max", "ServerGoodbye", {"reason": "internalError",
                                                   "message": "m" * 1024}),
        ok("notice-warning", "Notice", {"level": "warning", "code": "encoderFailure",
                                        "message": "hardware encoder failed, using software"}),
        ok("notice-info-empty", "Notice", {"level": "info", "code": "none", "message": ""}),
        ok("notice-error-cjk", "Notice", {"level": "error", "code": "permissionDenied",
                                          "message": "音频采集权限被拒绝"}),
        ok("notice-injection-denied", "Notice", {"level": "error", "code": "injectionDenied",
                                                 "message": "input injection is not permitted (INJECT_EVENTS)"}),
        ok("notice-clipboard-denied", "Notice", {"level": "error", "code": "clipboardDenied",
                                                 "message": "the clipboard refused access"}),
        ok("notice-audio-capture-failed", "Notice", {"level": "warning", "code": "audioCaptureFailed",
                                                     "message": "cannot start capturing the audio output"}),
        ok("notice-video-reduced", "Notice", {"level": "warning", "code": "videoReduced",
                                              "message": "video reduced to 810x1800 for the link"}),
        ok("notice-video-restored", "Notice", {"level": "info", "code": "videoRestored",
                                               "message": "video back to the setting, 1080x2400"}),
    ]


def bulk_vectors() -> list[dict]:
    many_apps = [{"package": f"com.example.app{i}", "label": f"应用 {i}", "versionCode": str(i * 1000 + 7),
                  "system": i % 5 == 0, "enabled": i % 7 != 0, "game": i % 3 == 0} for i in range(64)]
    return [
        ok("app-list-typical", "AppList", {"requestId": 1, "apps": [
            {"package": "com.example.game", "label": "Example Game", "versionCode": "412",
             "system": False, "enabled": True, "game": True},
            {"package": "com.android.settings", "label": "设置", "versionCode": "35",
             "system": True, "enabled": True, "game": False},
        ]}),
        ok("app-list-version-extremes", "AppList", {"requestId": 3, "apps": [
            {"package": "p" * 255, "label": "l" * 255, "versionCode": str(2**64 - 1),
             "system": True, "enabled": False, "game": True},
            {"package": "", "label": "", "versionCode": "0", "system": False, "enabled": False,
             "game": False},
        ]}),
        ok("app-list-empty", "AppList", {"requestId": 2, "apps": []}),
        ok("app-list-many", "AppList", {"requestId": 0xFFFFFFFF, "apps": many_apps}),
        ok("app-icon-typical", "AppIcon", {"requestId": 1, "package": "com.example.game",
                                           "png": "89504e470d0a1a0a0000000d49484452"}),
        ok("app-icon-empty", "AppIcon", {"requestId": 2, "package": "", "png": ""}),
        ok("app-icon-large", "AppIcon", {"requestId": 3, "package": "p" * 255,
                                         "png": pattern(4096, 0x89)}),
        ok("screenshot-typical", "Screenshot", {"requestId": 1, "width": 1080, "height": 2400,
                                                "format": "png",
                                                "data": "89504e470d0a1a0a"}),
        ok("screenshot-empty", "Screenshot", {"requestId": 0, "width": 0, "height": 0,
                                              "format": "png", "data": ""}),
        ok("screenshot-max-dimensions", "Screenshot", {"requestId": 0xFFFFFFFF,
                                                       "width": 0xFFFF, "height": 0xFFFF,
                                                       "format": "png",
                                                       "data": pattern(2048)}),
        ok("request-failed-not-found", "RequestFailed", {"requestId": 1, "code": "notFound",
                                                         "message": "package not found"}),
        ok("request-failed-busy-empty", "RequestFailed", {"requestId": 0,
                                                          "code": "resourceBusy",
                                                          "message": ""}),
        ok("request-failed-max", "RequestFailed", {"requestId": 0xFFFFFFFF,
                                                   "code": "internalError",
                                                   "message": "e" * 1024}),
    ]


def media_vectors() -> list[dict]:
    return [
        ok("video-session-portrait", "VideoSession", {
            "session": 1, "codec": "h264", "frameWidth": 1080, "frameHeight": 2400,
            "displayId": 0, "displayWidth": 1080, "displayHeight": 2400, "rotation": 0},
           channel="video"),
        ok("video-session-landscape", "VideoSession", {
            "session": 2, "codec": "h265", "frameWidth": 1920, "frameHeight": 864,
            "displayId": 0, "displayWidth": 2400, "displayHeight": 1080, "rotation": 1},
           channel="video"),
        ok("video-session-max", "VideoSession", {
            "session": 0xFFFF, "codec": "av1", "frameWidth": 0xFFFF, "frameHeight": 0xFFFF,
            "displayId": 0xFFFFFFFF, "displayWidth": 0xFFFF, "displayHeight": 0xFFFF,
            "rotation": 3}, channel="video"),
        ok("audio-session-stereo", "AudioSession", {"session": 1, "codec": "opus",
                                                    "sampleRate": 48000, "channels": 2},
           channel="audio"),
        ok("audio-session-mono", "AudioSession", {"session": 2, "codec": "opus",
                                                  "sampleRate": 44100, "channels": 1},
           channel="audio"),
        ok("audio-session-max", "AudioSession", {"session": 0xFFFF, "codec": "opus",
                                                 "sampleRate": 0xFFFFFFFF, "channels": 0xFF},
           channel="audio"),
        ok("video-config-sps", "MediaConfig", {"session": 1,
                                               "payload": "00000001674d401fe8802800b5"},
           channel="video"),
        ok("audio-config-opus-head", "MediaConfig", {"session": 1,
                                                     "payload": "4f707573486561640102"},
           channel="audio"),
        ok("video-config-empty", "MediaConfig", {"session": 3, "payload": ""}, channel="video"),
        ok("video-frame-key", "MediaFrame", {"session": 1, "keyFrame": True,
                                             "discardable": False, "ptsUs": "0",
                                             "encodeDelayUs": 0, "payload": "0000000165888400"},
           channel="video"),
        ok("video-frame-delta", "MediaFrame", {"session": 1, "keyFrame": False,
                                               "discardable": False, "ptsUs": "16666",
                                               "encodeDelayUs": 8250, "payload": "000000014188"},
           channel="video"),
        ok("video-frame-discardable", "MediaFrame", {"session": 1, "keyFrame": False,
                                                     "discardable": True, "ptsUs": "33333",
                                                     "encodeDelayUs": 12345, "payload": "000000010188"},
           channel="video"),
        ok("audio-frame", "MediaFrame", {"session": 1, "keyFrame": False, "discardable": False,
                                         "ptsUs": "20000", "encodeDelayUs": 21000,
                                         "payload": pattern(120)},
           channel="audio"),
        ok("video-frame-max-pts", "MediaFrame", {"session": 0xFFFF, "keyFrame": True,
                                                 "discardable": True, "ptsUs": str(2**63 - 1),
                                                 "encodeDelayUs": 0xFFFFFFFF, "payload": ""},
           channel="video"),
    ]


def companion_vectors() -> list[dict]:
    return [
        ok("companion-hello", "CompanionHello", {"version": 1, "token": pattern(16)}),
        ok("companion-hello-other-version", "CompanionHello", {"version": 0xFFFF, "token": "ff" * 16}),
        ok("companion-hello-empty-token", "CompanionHello", {"version": 1, "token": ""}),
        ok("cursor-state-center", "CursorState",
           {"x": 32768, "y": 32768, "visible": True, "pressed": False}),
        ok("cursor-state-pressed-corner", "CursorState",
           {"x": 65535, "y": 0, "visible": True, "pressed": True}),
        ok("cursor-state-hidden", "CursorState", {"x": 0, "y": 0, "visible": False, "pressed": False}),
        ok("companion-welcome-granted", "CompanionWelcome",
           {"version": 1, "overlay": "granted", "width": 1080, "height": 2400, "rotation": 0}),
        ok("companion-welcome-no-permission", "CompanionWelcome",
           {"version": 1, "overlay": "noPermission", "width": 0, "height": 0, "rotation": 1}),
        ok("overlay-changed-rotated", "OverlayChanged",
           {"overlay": "granted", "width": 2400, "height": 1080, "rotation": 1}),
        ok("overlay-changed-max", "OverlayChanged",
           {"overlay": "granted", "width": 0xFFFF, "height": 0xFFFF, "rotation": 3}),
        ok("companion-ping", "Ping", {"nonce": 7, "clientTimeUs": "123456789"}, channel="companion"),
        ok("companion-pong", "Pong", {"nonce": 7, "clientTimeUs": "123456789", "serverTimeUs": "987654321"},
           channel="companion"),
        ok("companion-goodbye", "Goodbye", {"reason": "none"}, channel="companion"),
        ok("companion-refused", "ServerGoodbye",
           {"reason": "authenticationFailed", "message": "wrong token"}, channel="companion"),
        ok("commit-text-ascii", "CommitText", {"text": "gg wp"}),
        ok("commit-text-chinese", "CommitText", {"text": "你好，世界"}),
        ok("commit-text-empty", "CommitText", {"text": ""}),
        ok("commit-text-max", "CommitText", {"text": "字" * 1365 + "a"}),
        ok("editor-action-enter", "EditorAction", {"action": "unspecified"}),
        ok("editor-action-send", "EditorAction", {"action": "send"}),
        ok("editor-action-previous", "EditorAction", {"action": "previous"}),
        ok("key-hint-layout-empty", "KeyHintLayout", {"hints": []}),
        ok("key-hint-layout-stick", "KeyHintLayout", {"hints": [
            key_hint("ring", 13107, 45875, "", 9830, 16384),
            key_hint("key", 13107, 37683, "W"),
            key_hint("key", 18022, 45875, "D"),
            key_hint("key", 13107, 54067, "S"),
            key_hint("key", 8192, 45875, "A"),
            key_hint("key", 13107, 45875, "Shift"),
            key_hint("camera", 49152, 32768, "`"),
            key_hint("tap", 58982, 52428, "空格"),
        ]}),
        ok("key-hint-layout-every-shape", "KeyHintLayout", {"hints": [
            key_hint(shape, 1000 * (i + 1), 2000 * (i + 1), "" if shape == "ring" else label,
                     4000 if shape == "ring" else 0, 6000 if shape == "ring" else 0)
            for i, (shape, label) in enumerate(zip(KEY_HINT_SHAPE, [
                "Q", "E", "R", "F", "`", "LMB", "Alt", "Ctrl+X", "↑", "", "Reload"]))
        ]}),
        ok("key-hint-layout-max", "KeyHintLayout", {"hints": [
            key_hint(KEY_HINT_SHAPE[i % len(KEY_HINT_SHAPE)], 65535 - i, i * 300, "k" * 32, 0xFFFF, 0xFFFF)
            for i in range(MAX_KEY_HINTS)
        ]}),
        ok("key-hint-state-faded", "KeyHintState", {"shown": True, "faded": True, "opacity": 60}),
        ok("key-hint-state-intro", "KeyHintState", {"shown": True, "faded": False, "opacity": 100}),
        ok("key-hint-state-hidden", "KeyHintState", {"shown": False, "faded": False, "opacity": 0}),
        ok("input-box-chat", "InputBoxChanged",
           {"shown": True, "multiLine": False, "inputType": 0x00004001, "action": "send"}),
        ok("input-box-multi-line", "InputBoxChanged",
           {"shown": True, "multiLine": True, "inputType": 0x00024001, "action": "unspecified"}),
        ok("input-box-number", "InputBoxChanged",
           {"shown": True, "multiLine": False, "inputType": 0x00000002, "action": "done"}),
        ok("input-box-hidden", "InputBoxChanged",
           {"shown": False, "multiLine": False, "inputType": 0, "action": "unspecified"}),
        ok("input-box-max", "InputBoxChanged",
           {"shown": True, "multiLine": True, "inputType": 0xFFFFFFFF, "action": "previous"}),
        ok("screenshot-request-default", "ScreenshotRequest", {"requestId": 1, "maxSide": 0, "quality": 90}),
        ok("screenshot-request-small", "ScreenshotRequest", {"requestId": 7, "maxSide": 1280, "quality": 1}),
        ok("screenshot-request-max", "ScreenshotRequest",
           {"requestId": 0xFFFFFFFF, "maxSide": 0xFFFF, "quality": 100}),
        ok("screenshot-chunk-first", "ScreenshotChunk",
           {"requestId": 1, "index": 0, "total": 3, "data": "ffd8ffe0" + pattern(SHOT_CHUNK_MAX - 4)}),
        ok("screenshot-chunk-last", "ScreenshotChunk", {"requestId": 1, "index": 2, "total": 3, "data": "ffd9"}),
        ok("screenshot-chunk-single", "ScreenshotChunk",
           {"requestId": 2, "index": 0, "total": 1, "data": "ffd8ffe0" + pattern(60, 9) + "ffd9"}),
        ok("screenshot-chunk-most", "ScreenshotChunk",
           {"requestId": 0xFFFFFFFF, "index": SHOT_CHUNKS_MAX - 1, "total": SHOT_CHUNKS_MAX,
            "data": pattern(SHOT_TOTAL_MAX - (SHOT_CHUNKS_MAX - 1) * SHOT_CHUNK_MAX, 5)}),
        ok("screenshot-failed-refused", "ScreenshotFailed",
           {"requestId": 1, "reason": "refused", "message": "the screen capture was not allowed on the phone"}),
        ok("screenshot-failed-unsupported", "ScreenshotFailed",
           {"requestId": 2, "reason": "unsupported", "message": ""}),
        ok("screenshot-failed-busy", "ScreenshotFailed",
           {"requestId": 3, "reason": "busy", "message": "a screenshot is still being taken"}),
        ok("screenshot-failed-error", "ScreenshotFailed",
           {"requestId": 0xFFFFFFFF, "reason": "error", "message": "无法读取画面"}),
        ok("companion-hello-version-2", "CompanionHello", {"version": 2, "token": pattern(16, 0x20)}),
        ok("companion-welcome-version-2", "CompanionWelcome",
           {"version": 2, "overlay": "granted", "width": 1080, "height": 2400, "rotation": 0}),
        ok("companion-settings-usb", "CompanionSettings",
           {"computer": pattern(16, 0xA0), "name": "DESKTOP-7Q2K1LM", "link": "usb", "accent": 0xFF6B3D,
            "applied": 0, "cursor": False, "hints": False, "hintOpacity": 60, "ime": True, "autoConnect": True}),
        ok("companion-settings-lan-applied", "CompanionSettings",
           {"computer": "ff" * 16, "name": "游戏电脑", "link": "lan", "accent": 0x22D3C5, "applied": 7,
            "cursor": True, "hints": True, "hintOpacity": 10, "ime": False, "autoConnect": False}),
        ok("companion-settings-wireless-empty-name", "CompanionSettings",
           {"computer": "00" * 16, "name": "", "link": "wireless", "accent": 0, "applied": 1,
            "cursor": False, "hints": True, "hintOpacity": 35, "ime": True, "autoConnect": False}),
        ok("companion-settings-max", "CompanionSettings",
           {"computer": pattern(16, 0xF0), "name": "n" * COMPUTER_NAME_MAX, "link": "wireless",
            "accent": ACCENT_MAX, "applied": 0xFFFFFFFF, "cursor": True, "hints": True,
            "hintOpacity": PHONE_HINT_OPACITY_MAX, "ime": True, "autoConnect": True}),
        ok("companion-setting-change-cursor-on", "CompanionSettingChange",
           {"change": 1, "item": "cursor", "value": 1}),
        ok("companion-setting-change-hints-off", "CompanionSettingChange",
           {"change": 2, "item": "hints", "value": 0}),
        ok("companion-setting-change-opacity", "CompanionSettingChange",
           {"change": 3, "item": "hintOpacity", "value": 35}),
        ok("companion-setting-change-opacity-least", "CompanionSettingChange",
           {"change": 4, "item": "hintOpacity", "value": PHONE_HINT_OPACITY_MIN}),
        ok("companion-setting-change-ime-off", "CompanionSettingChange",
           {"change": 5, "item": "ime", "value": 0}),
        ok("companion-setting-change-auto-connect-max", "CompanionSettingChange",
           {"change": 0xFFFFFFFF, "item": "autoConnect", "value": 1}),
    ]


def key_hint(shape: str, x: int, y: int, label: str, width: int = 0, height: int = 0) -> dict:
    return {"shape": shape, "x": x, "y": y, "width": width, "height": height, "label": label}


def companion_settings_payload(name: bytes = b"PC", link: int = 0, accent: int = 0xFF6B3D, cursor: int = 0,
                               opacity: int = 60) -> bytes:
    return (bytes(range(16)) + struct.pack(">B", len(name)) + name
            + struct.pack(">BIIBBBBB", link, accent, 0, cursor, 0, opacity, 1, 1))


def malformed_vectors() -> list[dict]:
    point = struct.pack(">BBHHH", 0, 0, 1, 2, 3)
    touch_payload = struct.pack(">HB", 1, 1) + point
    duplicate_points = (struct.pack(">HB", 1, 2) + struct.pack(">BBHHH", 0, 3, 1, 2, 3)
                        + struct.pack(">BBHHH", 2, 3, 4, 5, 6))
    welcome_resumed_2 = (b"\x00" * 4 + struct.pack(">H", 28) + b"\x00" * 4
                         + struct.pack(">I", 0) + b"\x02")
    long_text = struct.pack(">H", 4097) + b"a" * 4097
    long_descriptor = struct.pack(">HHHB", 1, 2, 3, 0) + struct.pack(">H", 4097) + b"\x00" * 4097
    welcome_17 = (b"\x00" * 4 + struct.pack(">H", 28) + b"\x00" + struct.pack(">B", 17)
                  + b"\x00" * 15)
    welcome_reserved = (b"\x00" * 4 + struct.pack(">H", 28) + b"\x00" + b"\x00" + b"\x00"
                        + b"\x00" + struct.pack(">I", 1 << 13))
    app_entry_reserved = struct.pack(">IHB", 1, 1, 0) + struct.pack(">BQB", 0, 1, 0x08)
    video_session = struct.pack(">BHHIHHB", 1, 1080, 2400, 0, 1080, 2400, 0)
    return [
        bad("frame-header-truncated", "control", "toServer", "Truncated",
            bytes.fromhex("0101000000")),
        bad("unknown-type", "control", "toServer", "UnknownType", raw_frame(0x01FF, b"")),
        bad("wrong-direction", "control", "toServer", "WrongDirection",
            raw_frame(0x8401, b"\x00" * 20)),
        bad("client-hello-on-control", "control", "toServer", "UnexpectedType",
            raw_frame(0x0002, b"")),
        bad("touch-on-bulk", "bulk", "toServer", "UnexpectedType", raw_frame(0x0101, b"")),
        bad("control-frame-too-large", "control", "toServer", "FrameTooLarge",
            raw_frame(0x0103, b"", length=CONTROL_MAX + 1)),
        bad("handshake-frame-too-large", "handshake", "toServer", "FrameTooLarge",
            raw_frame(0x0002, b"", length=HANDSHAKE_MAX + 1)),
        bad("bulk-frame-too-large", "bulk", "toClient", "FrameTooLarge",
            raw_frame(0x8502, b"", length=BULK_MAX + 1)),
        bad("payload-truncated", "control", "toServer", "Truncated",
            raw_frame(0x0101, touch_payload[:4], length=len(touch_payload))),
        bad("touch-field-truncated", "control", "toServer", "Truncated",
            raw_frame(0x0101, touch_payload[:-1])),
        bad("touch-trailing-bytes", "control", "toServer", "TrailingBytes",
            raw_frame(0x0101, touch_payload + b"\x00")),
        bad("touch-invalid-action", "control", "toServer", "InvalidEnum",
            raw_frame(0x0101, touch_payload[:3] + b"\x04" + touch_payload[4:])),
        bad("touch-pointer-out-of-range", "control", "toServer", "OutOfRange",
            raw_frame(0x0101, touch_payload[:4] + b"\x20" + touch_payload[5:])),
        bad("touch-frame-empty", "control", "toServer", "OutOfRange",
            raw_frame(0x0101, struct.pack(">HB", 1, 0))),
        bad("touch-frame-too-many-points", "control", "toServer", "LengthExceeded",
            raw_frame(0x0101, struct.pack(">HB", 1, 33))),
        bad("touch-frame-duplicate-pointer", "control", "toServer", "OutOfRange",
            raw_frame(0x0101, duplicate_points)),
        bad("touch-frame-duplicate-and-trailing", "control", "toServer", "TrailingBytes",
            raw_frame(0x0101, duplicate_points + b"\x00")),
        bad("key-invalid-action", "control", "toServer", "InvalidEnum",
            raw_frame(0x0102, b"\x02" + b"\x00" * 10)),
        bad("receiver-report-truncated", "control", "toServer", "Truncated",
            raw_frame(0x0403, b"\x00" * 47)),
        bad("receiver-report-trailing-bytes", "control", "toServer", "TrailingBytes",
            raw_frame(0x0403, b"\x00" * 49)),
        bad("display-power-invalid-bool", "control", "toServer", "InvalidBool",
            raw_frame(0x0303, b"\x02")),
        bad("show-touches-invalid-bool", "control", "toServer", "InvalidBool",
            raw_frame(0x030A, b"\x02")),
        bad("subscribe-status-period-too-short", "control", "toServer", "OutOfRange",
            raw_frame(0x030B, struct.pack(">BH", 1, STATUS_PERIOD_MIN - 1))),
        bad("subscribe-status-period-too-long", "control", "toServer", "OutOfRange",
            raw_frame(0x030B, struct.pack(">BH", 1, STATUS_PERIOD_MAX + 1))),
        bad("subscribe-status-invalid-bool", "control", "toServer", "InvalidBool",
            raw_frame(0x030B, struct.pack(">BH", 2, 2000))),
        bad("subscribe-status-truncated", "control", "toServer", "Truncated",
            raw_frame(0x030B, b"\x01\x07")),
        bad("device-status-battery-out-of-range", "control", "toClient", "OutOfRange",
            raw_frame(0x8305, struct.pack(">BBhH", 101, 0, 365, 230))),
        bad("device-status-battery-below-unknown", "control", "toClient", "OutOfRange",
            raw_frame(0x8305, struct.pack(">BBhH", UNKNOWN_BATTERY - 1, 0, 365, 230))),
        bad("device-status-cpu-out-of-range", "control", "toClient", "OutOfRange",
            raw_frame(0x8305, struct.pack(">BBhH", 82, 1, 365, 1001))),
        bad("device-status-cpu-below-unknown", "control", "toClient", "OutOfRange",
            raw_frame(0x8305, struct.pack(">BBhH", 82, 1, 365, UNKNOWN_CPU - 1))),
        bad("device-status-invalid-charging", "control", "toClient", "InvalidBool",
            raw_frame(0x8305, struct.pack(">BBhH", 82, 2, 365, 230))),
        bad("device-status-truncated", "control", "toClient", "Truncated",
            raw_frame(0x8305, struct.pack(">BBhB", 82, 1, 365, 0))),
        bad("device-status-trailing", "control", "toClient", "TrailingBytes",
            raw_frame(0x8305, struct.pack(">BBhHB", 82, 1, 365, 230, 0))),
        bad("device-status-out-of-range-and-trailing", "control", "toClient", "TrailingBytes",
            raw_frame(0x8305, struct.pack(">BBhHB", 101, 1, 365, 230, 0))),
        bad("subscribe-status-on-bulk", "bulk", "toServer", "UnexpectedType",
            raw_frame(0x030B, struct.pack(">BH", 1, 2000))),
        bad("video-paused-invalid-bool", "control", "toServer", "InvalidBool",
            raw_frame(0x0205, b"\xff")),
        bad("back-invalid-action", "control", "toServer", "InvalidEnum",
            raw_frame(0x0105, b"\x02")),
        bad("text-malformed-utf8", "control", "toServer", "MalformedUtf8",
            raw_frame(0x0103, bytes.fromhex("0002fffe"))),
        bad("text-length-exceeded", "control", "toServer", "LengthExceeded",
            raw_frame(0x0103, long_text)),
        bad("text-string-truncated", "control", "toServer", "Truncated",
            raw_frame(0x0103, bytes.fromhex("0005616263"))),
        bad("uhid-descriptor-too-long", "control", "toServer", "LengthExceeded",
            raw_frame(0x0110, long_descriptor)),
        bad("uhid-create-without-bus", "control", "toServer", "Truncated",
            raw_frame(0x0110, struct.pack(">HHHBH", 1, 2, 3, 0, 0))),
        bad("uhid-feature-too-long", "control", "toServer", "LengthExceeded",
            raw_frame(0x0113, struct.pack(">HBH", 1, 2, 513) + b"\x00" * 513)),
        bad("uhid-feature-truncated", "control", "toServer", "Truncated",
            raw_frame(0x0113, struct.pack(">HBH", 1, 2, 4) + b"\x02\x00")),
        bad("uhid-feature-on-bulk", "bulk", "toServer", "UnexpectedType",
            raw_frame(0x0113, struct.pack(">HBH", 1, 2, 0))),
        bad("uhid-status-invalid-recognized", "control", "toClient", "InvalidBool",
            raw_frame(0x8111, struct.pack(">HBB", 1, 2, 0))),
        bad("uhid-status-invalid-vibration", "control", "toClient", "InvalidBool",
            raw_frame(0x8111, struct.pack(">HBB", 1, 1, 7))),
        bad("uhid-status-trailing", "control", "toClient", "TrailingBytes",
            raw_frame(0x8111, struct.pack(">HBBB", 1, 1, 1, 0))),
        bad("uhid-status-to-server", "control", "toServer", "WrongDirection",
            raw_frame(0x8111, struct.pack(">HBB", 1, 1, 1))),
        bad("start-app-reserved-flags", "control", "toServer", "ReservedBitsSet",
            raw_frame(0x0305, bytes.fromhex("016104"))),
        bad("configure-video-invalid-codec", "control", "toServer", "InvalidEnum",
            raw_frame(0x0201, b"\x00" + b"\x00" * 10 + b"\x00" + b"\x00" * 10)),
        bad("request-icon-size-out-of-range", "control", "toServer", "OutOfRange",
            raw_frame(0x0308, struct.pack(">IBH", 1, 0, 15))),
        bad("goodbye-invalid-reason", "control", "toServer", "InvalidEnum",
            raw_frame(0x0402, struct.pack(">H", len(ERROR_CODE)))),
        bad("request-key-frame-trailing", "control", "toServer", "TrailingBytes",
            raw_frame(0x0204, b"\x00")),
        bad("channel-open-control-channel", "handshake", "toServer", "InvalidEnum",
            raw_frame(0x0005, b"\x00" + b"\x00" * 32)),
        bad("server-hello-bad-magic", "handshake", "toClient", "BadMagic",
            raw_frame(0x8001, struct.pack(">IH", MAGIC + 1, 1) + b"\x00" * 16)),
        bad("welcome-too-many-displays", "handshake", "toClient", "LengthExceeded",
            raw_frame(0x8003, welcome_17)),
        bad("welcome-reserved-capability", "handshake", "toClient", "ReservedBitsSet",
            raw_frame(0x8003, welcome_reserved)),
        bad("welcome-invalid-resumed", "handshake", "toClient", "InvalidBool",
            raw_frame(0x8003, welcome_resumed_2)),
        bad("display-rotation-out-of-range", "control", "toClient", "OutOfRange",
            raw_frame(0x8303, struct.pack(">IHHBHIHHHH", 0, 1080, 2400, 4, 440, 60000,
                                          0, 0, 0, 0))),
        bad("foreground-app-reserved-flags", "control", "toClient", "ReservedBitsSet",
            raw_frame(0x8304, bytes.fromhex("01610002"))),
        bad("foreground-app-without-flags", "control", "toClient", "Truncated",
            raw_frame(0x8304, bytes.fromhex("016100"))),
        bad("app-list-too-many", "bulk", "toClient", "LengthExceeded",
            raw_frame(0x8501, struct.pack(">IH", 1, 4097))),
        bad("app-list-reserved-flags", "bulk", "toClient", "ReservedBitsSet",
            raw_frame(0x8501, app_entry_reserved)),
        bad("app-list-entry-truncated", "bulk", "toClient", "Truncated",
            raw_frame(0x8501, app_entry_reserved[:-3])),
        bad("cursor-state-reserved-flags", "companion", "toServer", "ReservedBitsSet",
            raw_frame(0x0602, struct.pack(">HHB", 1, 2, 0x04))),
        bad("cursor-state-truncated", "companion", "toServer", "Truncated",
            raw_frame(0x0602, struct.pack(">HH", 1, 2))),
        bad("cursor-state-trailing", "companion", "toServer", "TrailingBytes",
            raw_frame(0x0602, struct.pack(">HHBB", 1, 2, 1, 0))),
        bad("companion-hello-token-too-long", "companion", "toServer", "LengthExceeded",
            raw_frame(0x0601, struct.pack(">HB", 1, 17) + b"\x00" * 17)),
        bad("companion-hello-token-truncated", "companion", "toServer", "Truncated",
            raw_frame(0x0601, struct.pack(">HB", 1, 16) + b"\x00" * 15)),
        bad("companion-welcome-rotation-out-of-range", "companion", "toClient", "OutOfRange",
            raw_frame(0x8601, struct.pack(">HBHHB", 1, 0, 1080, 2400, 4))),
        bad("overlay-changed-invalid-status", "companion", "toClient", "InvalidEnum",
            raw_frame(0x8602, struct.pack(">BHHB", 2, 1080, 2400, 0))),
        bad("cursor-state-on-control", "control", "toServer", "UnexpectedType",
            raw_frame(0x0602, struct.pack(">HHB", 1, 2, 1))),
        bad("touch-on-companion", "companion", "toServer", "UnexpectedType",
            raw_frame(0x0101, touch_payload)),
        bad("receiver-report-on-companion", "companion", "toServer", "UnexpectedType",
            raw_frame(0x0403, b"\x00" * 48)),
        bad("companion-welcome-on-handshake", "handshake", "toClient", "UnexpectedType",
            raw_frame(0x8601, struct.pack(">HBHHB", 1, 0, 1080, 2400, 0))),
        bad("companion-wrong-direction", "companion", "toServer", "WrongDirection",
            raw_frame(0x8601, struct.pack(">HBHHB", 1, 0, 1080, 2400, 0))),
        bad("companion-frame-too-large", "companion", "toServer", "FrameTooLarge",
            raw_frame(0x0601, b"", length=COMPANION_MAX + 1)),
        bad("commit-text-malformed-utf8", "companion", "toServer", "MalformedUtf8",
            raw_frame(0x0603, bytes.fromhex("0002fffe"))),
        bad("commit-text-too-long", "companion", "toServer", "LengthExceeded",
            raw_frame(0x0603, struct.pack(">H", 4097) + b"a" * 4097)),
        bad("commit-text-on-control", "control", "toServer", "UnexpectedType",
            raw_frame(0x0603, struct.pack(">H", 0))),
        bad("editor-action-invalid", "companion", "toServer", "InvalidEnum",
            raw_frame(0x0604, b"\x08")),
        bad("editor-action-trailing", "companion", "toServer", "TrailingBytes",
            raw_frame(0x0604, b"\x00\x00")),
        bad("key-hint-layout-too-many", "companion", "toServer", "LengthExceeded",
            raw_frame(0x0605, struct.pack(">B", MAX_KEY_HINTS + 1))),
        bad("key-hint-layout-invalid-shape", "companion", "toServer", "InvalidEnum",
            raw_frame(0x0605, struct.pack(">BBHHHHB", 1, len(KEY_HINT_SHAPE), 0, 0, 0, 0, 0))),
        bad("key-hint-layout-label-too-long", "companion", "toServer", "LengthExceeded",
            raw_frame(0x0605, struct.pack(">BBHHHHB", 1, 0, 0, 0, 0, 0, 33) + b"x" * 33)),
        bad("key-hint-layout-truncated", "companion", "toServer", "Truncated",
            raw_frame(0x0605, struct.pack(">BBHHHHB", 2, 0, 0, 0, 0, 0, 0))),
        bad("key-hint-state-reserved-flags", "companion", "toServer", "ReservedBitsSet",
            raw_frame(0x0606, struct.pack(">BB", 0x04, 60))),
        bad("key-hint-state-opacity-out-of-range", "companion", "toServer", "OutOfRange",
            raw_frame(0x0606, struct.pack(">BB", 0x01, 101))),
        bad("input-box-reserved-flags", "companion", "toClient", "ReservedBitsSet",
            raw_frame(0x8603, struct.pack(">BIB", 0x04, 1, 0))),
        bad("input-box-invalid-action", "companion", "toClient", "InvalidEnum",
            raw_frame(0x8603, struct.pack(">BIB", 0x01, 1, len(IME_ACTION)))),
        bad("input-box-truncated", "companion", "toClient", "Truncated",
            raw_frame(0x8603, struct.pack(">BI", 0x01, 1))),
        bad("input-box-wrong-direction", "companion", "toServer", "WrongDirection",
            raw_frame(0x8603, struct.pack(">BIB", 0x01, 1, 0))),
        bad("input-box-on-control", "control", "toClient", "UnexpectedType",
            raw_frame(0x8603, struct.pack(">BIB", 0x01, 1, 0))),
        bad("screenshot-request-quality-zero", "companion", "toServer", "OutOfRange",
            raw_frame(0x0607, struct.pack(">IHB", 1, 0, 0))),
        bad("screenshot-request-quality-too-high", "companion", "toServer", "OutOfRange",
            raw_frame(0x0607, struct.pack(">IHB", 1, 0, 101))),
        bad("screenshot-request-truncated", "companion", "toServer", "Truncated",
            raw_frame(0x0607, struct.pack(">IH", 1, 0))),
        bad("screenshot-request-on-control", "control", "toServer", "UnexpectedType",
            raw_frame(0x0607, struct.pack(">IHB", 1, 0, 90))),
        bad("screenshot-chunk-index-beyond-total", "companion", "toClient", "OutOfRange",
            raw_frame(0x8604, struct.pack(">IHHH", 1, 3, 3, 1) + b"\xff")),
        bad("screenshot-chunk-index-beyond-and-trailing", "companion", "toClient", "TrailingBytes",
            raw_frame(0x8604, struct.pack(">IHHH", 1, 3, 3, 1) + b"\xff\x00")),
        bad("screenshot-chunk-total-zero", "companion", "toClient", "OutOfRange",
            raw_frame(0x8604, struct.pack(">IHHH", 1, 0, 0, 0))),
        bad("screenshot-chunk-total-too-many", "companion", "toClient", "OutOfRange",
            raw_frame(0x8604, struct.pack(">IHHH", 1, 0, SHOT_CHUNKS_MAX + 1, 0))),
        bad("screenshot-chunk-too-long", "companion", "toClient", "LengthExceeded",
            raw_frame(0x8604, struct.pack(">IHHH", 1, 0, 1, SHOT_CHUNK_MAX + 1) + b"\x00" * (SHOT_CHUNK_MAX + 1))),
        bad("screenshot-chunk-data-truncated", "companion", "toClient", "Truncated",
            raw_frame(0x8604, struct.pack(">IHHH", 1, 0, 1, 4) + b"\xff\xd8")),
        bad("screenshot-chunk-wrong-direction", "companion", "toServer", "WrongDirection",
            raw_frame(0x8604, struct.pack(">IHHH", 1, 0, 1, 1) + b"\xff")),
        bad("screenshot-failed-invalid-reason", "companion", "toClient", "InvalidEnum",
            raw_frame(0x8605, struct.pack(">IBH", 1, len(SCREENSHOT_PROBLEM), 0))),
        bad("screenshot-failed-truncated", "companion", "toClient", "Truncated",
            raw_frame(0x8605, struct.pack(">IB", 1, 0))),
        bad("screenshot-failed-on-bulk", "bulk", "toClient", "UnexpectedType",
            raw_frame(0x8605, struct.pack(">IBH", 1, 0, 0))),
        bad("companion-settings-name-too-long", "companion", "toServer", "LengthExceeded",
            raw_frame(0x0608, companion_settings_payload(name=b"n" * (COMPUTER_NAME_MAX + 1)))),
        bad("companion-settings-name-malformed-utf8", "companion", "toServer", "MalformedUtf8",
            raw_frame(0x0608, companion_settings_payload(name=b"\xff\xfe"))),
        bad("companion-settings-invalid-link", "companion", "toServer", "InvalidEnum",
            raw_frame(0x0608, companion_settings_payload(link=len(COMPUTER_LINK)))),
        bad("companion-settings-accent-reserved", "companion", "toServer", "ReservedBitsSet",
            raw_frame(0x0608, companion_settings_payload(accent=0x01FF6B3D))),
        bad("companion-settings-invalid-bool", "companion", "toServer", "InvalidBool",
            raw_frame(0x0608, companion_settings_payload(cursor=2))),
        bad("companion-settings-opacity-too-low", "companion", "toServer", "OutOfRange",
            raw_frame(0x0608, companion_settings_payload(opacity=PHONE_HINT_OPACITY_MIN - 1))),
        bad("companion-settings-opacity-too-high", "companion", "toServer", "OutOfRange",
            raw_frame(0x0608, companion_settings_payload(opacity=PHONE_HINT_OPACITY_MAX + 1))),
        bad("companion-settings-truncated", "companion", "toServer", "Truncated",
            raw_frame(0x0608, companion_settings_payload()[:-1])),
        bad("companion-settings-trailing", "companion", "toServer", "TrailingBytes",
            raw_frame(0x0608, companion_settings_payload() + b"\x00")),
        bad("companion-settings-wrong-direction", "companion", "toClient", "WrongDirection",
            raw_frame(0x0608, companion_settings_payload())),
        bad("companion-settings-on-control", "control", "toServer", "UnexpectedType",
            raw_frame(0x0608, companion_settings_payload())),
        bad("companion-setting-change-invalid-item", "companion", "toClient", "InvalidEnum",
            raw_frame(0x8606, struct.pack(">IBB", 1, len(COMPANION_ITEM), 1))),
        bad("companion-setting-change-invalid-item-before-zero", "companion", "toClient", "InvalidEnum",
            raw_frame(0x8606, struct.pack(">IBB", 0, len(COMPANION_ITEM), 1))),
        bad("companion-setting-change-zero", "companion", "toClient", "OutOfRange",
            raw_frame(0x8606, struct.pack(">IBB", 0, 0, 1))),
        bad("companion-setting-change-switch-two", "companion", "toClient", "OutOfRange",
            raw_frame(0x8606, struct.pack(">IBB", 1, COMPANION_ITEM.index("cursor"), 2))),
        bad("companion-setting-change-opacity-too-low", "companion", "toClient", "OutOfRange",
            raw_frame(0x8606, struct.pack(">IBB", 1, COMPANION_ITEM.index("hintOpacity"),
                                          PHONE_HINT_OPACITY_MIN - 1))),
        bad("companion-setting-change-opacity-too-high", "companion", "toClient", "OutOfRange",
            raw_frame(0x8606, struct.pack(">IBB", 1, COMPANION_ITEM.index("hintOpacity"),
                                          PHONE_HINT_OPACITY_MAX + 1))),
        bad("companion-setting-change-truncated", "companion", "toClient", "Truncated",
            raw_frame(0x8606, struct.pack(">IB", 1, 0))),
        bad("companion-setting-change-trailing", "companion", "toClient", "TrailingBytes",
            raw_frame(0x8606, struct.pack(">IBBB", 1, 0, 1, 0))),
        bad("companion-setting-change-wrong-direction", "companion", "toServer", "WrongDirection",
            raw_frame(0x8606, struct.pack(">IBB", 1, 0, 1))),
        bad("companion-setting-change-on-control", "control", "toClient", "UnexpectedType",
            raw_frame(0x8606, struct.pack(">IBB", 1, 0, 1))),
        bad("media-header-truncated", "video", "toClient", "Truncated",
            media_header(2, 0, 1, 0, 0)[:19]),
        bad("media-invalid-kind", "video", "toClient", "InvalidEnum",
            media_header(3, 0, 1, 0, 0)),
        bad("media-frame-reserved-flags", "video", "toClient", "ReservedBitsSet",
            media_header(2, 0x04, 1, 0, 0)),
        bad("media-config-discardable-flag", "video", "toClient", "ReservedBitsSet",
            media_header(1, 0x02, 1, 0, 0)),
        bad("media-session-key-flag", "video", "toClient", "ReservedBitsSet",
            media_header(0, 0x01, 1, 0, len(video_session)) + video_session),
        bad("media-session-zero", "video", "toClient", "OutOfRange",
            media_header(2, 0, 0, 0, 0)),
        bad("media-config-nonzero-pts", "video", "toClient", "OutOfRange",
            media_header(1, 0, 1, 5, 0)),
        bad("media-config-nonzero-encode-delay", "video", "toClient", "OutOfRange",
            media_header(1, 0, 1, 0, 0, encode_delay=7)),
        bad("media-session-nonzero-encode-delay", "audio", "toClient", "OutOfRange",
            media_header(0, 0, 1, 0, 6, encode_delay=1) + struct.pack(">BIB", 1, 48000, 2)),
        bad("video-packet-too-large", "video", "toClient", "FrameTooLarge",
            media_header(2, 0, 1, 0, VIDEO_MAX + 1)),
        bad("audio-packet-too-large", "audio", "toClient", "FrameTooLarge",
            media_header(2, 0, 1, 0, AUDIO_MAX + 1)),
        bad("media-payload-truncated", "video", "toClient", "Truncated",
            media_header(2, 0, 1, 0, 10) + b"\x00" * 3),
        bad("video-session-trailing", "video", "toClient", "TrailingBytes",
            media_header(0, 0, 1, 0, len(video_session) + 1) + video_session + b"\x00"),
        bad("video-session-truncated", "video", "toClient", "Truncated",
            media_header(0, 0, 1, 0, len(video_session) - 1) + video_session[:-1]),
        bad("video-session-rotation", "video", "toClient", "OutOfRange",
            media_header(0, 0, 1, 0, len(video_session)) + video_session[:-1] + b"\x04"),
        bad("audio-session-invalid-codec", "audio", "toClient", "InvalidEnum",
            media_header(0, 0, 1, 0, 6) + struct.pack(">BIB", 2, 48000, 2)),
    ]


GF_POLYNOMIAL = 0x11D
GF_EXP = [0] * 512
GF_LOG = [0] * 256
_gf_value = 1
for _gf_power in range(255):
    GF_EXP[_gf_power] = _gf_value
    GF_LOG[_gf_value] = _gf_power
    _gf_value <<= 1
    if _gf_value & 0x100:
        _gf_value ^= GF_POLYNOMIAL
for _gf_power in range(255, 512):
    GF_EXP[_gf_power] = GF_EXP[_gf_power - 255]


def gf_mul(a: int, b: int) -> int:
    return 0 if a == 0 or b == 0 else GF_EXP[GF_LOG[a] + GF_LOG[b]]


def gf_inv(a: int) -> int:
    assert a != 0
    return GF_EXP[255 - GF_LOG[a]]


def rs_rows(k: int, m: int) -> list[list[int]]:
    assert 1 <= k and 0 <= m and k + m <= 256
    identity = [[1 if j == i else 0 for j in range(k)] for i in range(k)]
    return identity + [[gf_inv((k + i) ^ j) for j in range(k)] for i in range(m)]


def rs_combine(rows: list[list[int]], shards: list[bytes]) -> list[bytes]:
    length = len(shards[0])
    out = []
    for row in rows:
        combined = bytearray(length)
        for coefficient, shard in zip(row, shards):
            if coefficient:
                for b in range(length):
                    combined[b] ^= gf_mul(coefficient, shard[b])
        out.append(bytes(combined))
    return out


def gf_invert(matrix: list[list[int]]) -> list[list[int]]:
    n = len(matrix)
    work = [row[:] + [1 if j == i else 0 for j in range(n)] for i, row in enumerate(matrix)]
    for column in range(n):
        pivot = next(r for r in range(column, n) if work[r][column])
        work[column], work[pivot] = work[pivot], work[column]
        scale = gf_inv(work[column][column])
        work[column] = [gf_mul(scale, value) for value in work[column]]
        for r in range(n):
            factor = work[r][column]
            if r != column and factor:
                work[r] = [value ^ gf_mul(factor, top) for value, top in zip(work[r], work[column])]
    return [row[n:] for row in work]


def rs_reconstruct(shards: list, k: int, m: int) -> list[bytes]:
    present = [i for i, shard in enumerate(shards) if shard is not None][:k]
    assert len(present) == k
    inverse = gf_invert([rs_rows(k, m)[i] for i in present])
    rebuilt = rs_combine(inverse, [shards[i] for i in present])
    return [shards[d] if shards[d] is not None else rebuilt[d] for d in range(k)]


def fec_vector(name: str, data: list[bytes], m: int, erasures: list[list[int]]) -> dict:
    k = len(data)
    assert len({len(shard) for shard in data}) == 1
    parity = rs_combine(rs_rows(k, m)[k:], data)
    shards = data + parity
    for lost in erasures:
        assert len(lost) <= m
        assert rs_reconstruct([None if i in lost else s for i, s in enumerate(shards)], k, m) == data, (name, lost)
    return {"name": name, "k": k, "m": m, "data": [d.hex() for d in data], "parity": [p.hex() for p in parity],
            "erasures": erasures}


def fec_vectors() -> list[dict]:
    def shards(k: int, length: int, seed: int) -> list[bytes]:
        return [bytes((seed * 31 + j * 17 + b * 7 + (j * b) % 13) & 0xFF for b in range(length)) for j in range(k)]

    return [
        fec_vector("one-shard-no-parity", [bytes.fromhex("5a")], 0, []),
        fec_vector("one-shard-mirrored", [bytes.fromhex("0102030405")], 1, [[0]]),
        fec_vector("two-plus-one", shards(2, 6, 1), 1, [[0], [1], [2]]),
        fec_vector("zeros", [bytes(8)] * 3, 2, [[0, 1], [2, 4]]),
        fec_vector("all-ff", [b"\xff" * 8] * 3, 2, [[1, 2]]),
        fec_vector("four-plus-two", shards(4, 16, 2), 2, [[0], [5], [0, 1], [3, 5], [4, 5], [1, 4]]),
        fec_vector("ten-plus-four", shards(10, 64, 3), 4, [[0, 1, 2, 3], [9, 10, 11, 12], [2, 5, 11, 13]]),
        fec_vector("largest-group", shards(64, 8, 4), 16,
                   [list(range(16)), list(range(0, 64, 4)), list(range(48, 64)), list(range(56, 72))]),
        fec_vector("field-limit-data", shards(255, 2, 5), 1, [[0], [254], [255]]),
        fec_vector("field-limit-parity", shards(1, 3, 6), 255, [[0], list(range(255))]),
    ]


DATAGRAM_MAGIC = 0x4753
DATAGRAM_HANDSHAKE_CHANNEL = 15
UDP_HELLO_CONTEXT = b"GSCP/1 udp hello"
UDP_CONTROL_CONTEXT = b"GSCP/1 udp control"
UDP_TOKEN = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
UDP_NONCE = bytes.fromhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf")
UDP_CONNECTION = 0x12345678


def datagram(channel: int, connection: int, payload: bytes, flags: int = 0) -> bytes:
    return struct.pack(">HBBI", DATAGRAM_MAGIC, channel, flags, connection) + payload


def udp_hello_proof(token: bytes, nonce: bytes) -> bytes:
    return hmac.new(token, UDP_HELLO_CONTEXT + nonce, hashlib.sha256).digest()


def control_tag(token: bytes, preceding: bytes) -> bytes:
    return hmac.new(token, UDP_CONTROL_CONTEXT + preceding, hashlib.sha256).digest()[:16]


RELIABLE_DATAGRAM_LIMIT = 1200
CONTROL_CHANNEL = 0
BULK_CHANNEL = 3
SEGMENT_HEADER_SIZE = 6
CONTROL_OVERHEAD = 8 + 8 + 9 + 16
BULK_OVERHEAD = 8 + 9
MAX_CONTROL_SEGMENT = RELIABLE_DATAGRAM_LIMIT - CONTROL_OVERHEAD - SEGMENT_HEADER_SIZE
MAX_BULK_SEGMENT = RELIABLE_DATAGRAM_LIMIT - BULK_OVERHEAD - SEGMENT_HEADER_SIZE

Segment = tuple[int, bytes]


def reliable_block(ack: int, sack: int, segments: list[Segment]) -> bytes:
    out = struct.pack(">IIB", ack, sack, len(segments))
    for seq, data in segments:
        out += struct.pack(">IH", seq, len(data)) + data
    return out


def control_body(counter: int, ack: int, sack: int, segments: list[Segment]) -> bytes:
    return datagram(CONTROL_CHANNEL, UDP_CONNECTION, struct.pack(">Q", counter) + reliable_block(ack, sack, segments))


def control_datagram(counter: int, ack: int, sack: int, segments: list[Segment]) -> bytes:
    body = control_body(counter, ack, sack, segments)
    return body + control_tag(UDP_TOKEN, body)


def bulk_datagram(ack: int, sack: int, segments: list[Segment]) -> bytes:
    return datagram(BULK_CHANNEL, UDP_CONNECTION, reliable_block(ack, sack, segments))


def pattern_bytes(size: int, seed: int) -> bytes:
    return bytes((seed + 7 * i) & 0xFF for i in range(size))


def reliable_vectors() -> list[dict]:
    def entry(name: str, channel: int, ack: int, sack: int, segments: list[Segment], counter: int = 0) -> dict:
        if channel == CONTROL_CHANNEL:
            data = control_datagram(counter, ack, sack, segments)
        else:
            data = bulk_datagram(ack, sack, segments)
        assert len(data) <= RELIABLE_DATAGRAM_LIMIT
        out = {"name": name, "channel": channel}
        if channel == CONTROL_CHANNEL:
            out["counter"] = str(counter)
        out.update({"ack": ack, "sack": sack, "segments": [{"seq": seq, "hex": data_.hex()} for seq, data_ in segments],
                    "hex": data.hex()})
        return out

    def tagged(body: bytes) -> bytes:
        return body + control_tag(UDP_TOKEN, body)

    def control_raw(counter: int, block: bytes) -> bytes:
        return datagram(CONTROL_CHANNEL, UDP_CONNECTION, struct.pack(">Q", counter) + block)

    one = [(0, pattern_bytes(24, 1))]
    datagrams = [
        entry("control-pure-ack", CONTROL_CHANNEL, 0, 0, [], counter=1),
        entry("control-first-message", CONTROL_CHANNEL, 0, 0, one, counter=2),
        entry("control-piggyback", CONTROL_CHANNEL, 12, 0b101,
              [(3, pattern_bytes(20, 3)), (4, pattern_bytes(31, 4)), (5, pattern_bytes(18, 5)),
               (6, pattern_bytes(40, 6))], counter=7),
        entry("control-big-numbers", CONTROL_CHANNEL, 0xFFFFFFFE, 0x80000001, [], counter=0x0102030405060708),
        entry("control-largest", CONTROL_CHANNEL, 0, 0, [(0, pattern_bytes(MAX_CONTROL_SEGMENT, 9))], counter=9),
        entry("bulk-pure-ack", BULK_CHANNEL, 3, 0, []),
        entry("bulk-two-segments", BULK_CHANNEL, 0, 0, [(0, pattern_bytes(100, 10)), (1, pattern_bytes(200, 11))]),
        entry("bulk-largest", BULK_CHANNEL, 5, 0xFFFFFFFF, [(40, pattern_bytes(MAX_BULK_SEGMENT, 12))]),
    ]
    assert len(bytes.fromhex(datagrams[4]["hex"])) == RELIABLE_DATAGRAM_LIMIT
    assert len(bytes.fromhex(datagrams[-1]["hex"])) == RELIABLE_DATAGRAM_LIMIT

    good = control_datagram(3, 0, 0, one)
    bad_tag = bytearray(good)
    bad_tag[-1] ^= 0x01
    segment_header = struct.pack(">IH", 0, 50)
    malformed = [
        {"name": "control-short", "channel": 0, "hex": good[:CONTROL_OVERHEAD - 1].hex(), "error": "Truncated"},
        {"name": "control-bad-tag", "channel": 0, "hex": bytes(bad_tag).hex(), "error": "BadTag"},
        {"name": "control-zero-counter", "channel": 0, "hex": control_datagram(0, 0, 0, one).hex(),
         "error": "OutOfRange"},
        {"name": "control-segment-overruns", "channel": 0,
         "hex": tagged(control_raw(4, struct.pack(">IIB", 0, 0, 1) + segment_header + bytes(10))).hex(),
         "error": "Truncated"},
        {"name": "control-segment-header-cut", "channel": 0,
         "hex": tagged(control_raw(5, struct.pack(">IIB", 0, 0, 2) + struct.pack(">IH", 0, 24) + pattern_bytes(24, 1)
                                   + b"\x00\x00\x00")).hex(),
         "error": "Truncated"},
        {"name": "control-empty-segment", "channel": 0,
         "hex": tagged(control_raw(6, struct.pack(">IIBIH", 0, 0, 1, 0, 0))).hex(), "error": "OutOfRange"},
        {"name": "control-trailing", "channel": 0,
         "hex": tagged(control_raw(7, reliable_block(0, 0, one) + b"\x00\x00")).hex(), "error": "TrailingBytes"},
        {"name": "bulk-short", "channel": 3, "hex": bulk_datagram(0, 0, [])[:BULK_OVERHEAD - 1].hex(),
         "error": "Truncated"},
        {"name": "bulk-segment-overruns", "channel": 3,
         "hex": datagram(BULK_CHANNEL, UDP_CONNECTION, struct.pack(">IIB", 0, 0, 1) + segment_header + bytes(49)).hex(),
         "error": "Truncated"},
        {"name": "bulk-empty-segment", "channel": 3,
         "hex": datagram(BULK_CHANNEL, UDP_CONNECTION, struct.pack(">IIBIH", 0, 0, 1, 0, 0)).hex(),
         "error": "OutOfRange"},
        {"name": "bulk-trailing", "channel": 3,
         "hex": (bulk_datagram(0, 0, one) + b"\x00").hex(), "error": "TrailingBytes"},
    ]
    return [{
        "token": UDP_TOKEN.hex(),
        "connection": UDP_CONNECTION,
        "maxSegment": {"control": MAX_CONTROL_SEGMENT, "bulk": MAX_BULK_SEGMENT},
        "datagrams": datagrams,
        "malformed": malformed,
    }]


VIDEO_CHANNEL = 1
AUDIO_CHANNEL = 2
SHARD_HEADER_SIZE = 16
MAX_SHARD_SIZE = RELIABLE_DATAGRAM_LIMIT - 8 - SHARD_HEADER_SIZE
SHARDS_PER_GROUP = 64
MAX_MEDIA_PACKET = 16 * 1024 * 1024 + 20


def shard_header(packet: int, packet_size: int, shard_size: int, group: int, index: int, k: int, m: int,
                 reserved: int = 0) -> bytes:
    return struct.pack(">IIHHBBBB", packet, packet_size, shard_size, group, index, k, m, reserved)


def video_datagrams(number: int, packet: bytes, shard_size: int, parity: list[int]) -> list[bytes]:
    count = -(-len(packet) // shard_size)
    groups = -(-count // SHARDS_PER_GROUP)
    assert len(parity) == groups
    out = []
    for group in range(groups):
        k = min(SHARDS_PER_GROUP, count - SHARDS_PER_GROUP * group)
        m = parity[group]
        first = SHARDS_PER_GROUP * group
        data = [packet[(first + i) * shard_size:(first + i + 1) * shard_size] for i in range(k)]
        padded = [shard + bytes(shard_size - len(shard)) for shard in data]
        parities = rs_combine(rs_rows(k, m)[k:], padded) if m else []
        for index, shard in enumerate(data + parities):
            header = shard_header(number, len(packet), shard_size, group, index, k, m)
            out.append(datagram(VIDEO_CHANNEL, UDP_CONNECTION, header + shard))
    return out


def audio_datagram(number: int, current: bytes, previous: bytes | None) -> bytes:
    entries = [current] + ([previous] if previous is not None else [])
    body = struct.pack(">IB", number, len(entries)) + b"".join(struct.pack(">H", len(e)) + e for e in entries)
    return datagram(AUDIO_CHANNEL, UDP_CONNECTION, body)


def media_bytes(kind: int, flags: int, session: int, pts: int, delay: int, payload: bytes) -> bytes:
    return struct.pack(">BBHqII", kind, flags, session, pts, delay, len(payload)) + payload


def recommended_shard_size(packet_size: int) -> int:
    return -(-packet_size // -(-packet_size // MAX_SHARD_SIZE))


def direct_media_vectors() -> list[dict]:
    def video_case(name: str, number: int, packet: bytes, shard_size: int, parity: list[int],
                   losses: list[tuple[list[int], bool]]) -> dict:
        datagrams = video_datagrams(number, packet, shard_size, parity)
        assert all(len(d) <= RELIABLE_DATAGRAM_LIMIT for d in datagrams)
        return {"name": name, "packet": number, "bytes": packet.hex(), "shardSize": shard_size, "parity": parity,
                "datagrams": [d.hex() for d in datagrams],
                "losses": [{"lost": lost, "recovered": recovered} for lost, recovered in losses]}

    key = media_bytes(2, 1, 1, 1_000_000, 4_000, pattern_bytes(300, 21))
    small = media_bytes(2, 0, 1, 1_033_333, 3_500, pattern_bytes(30, 22))
    two_groups = media_bytes(2, 0, 1, 1_066_666, 3_000, pattern_bytes(16 * 69 + 5 - 20, 23))
    largest = media_bytes(2, 1, 2, 2_000_000, 5_000, pattern_bytes(2 * MAX_SHARD_SIZE - 20, 24))
    assert recommended_shard_size(len(largest)) == MAX_SHARD_SIZE
    video = [
        video_case("key-frame-one-shard", 0, key, recommended_shard_size(len(key)), [1],
                   [([0], True), ([1], True), ([0, 1], False)]),
        video_case("no-parity", 1, small, 20, [0], [([1], False)]),
        video_case("two-groups", 2, two_groups, 16, [3, 2],
                   [([0, 10, 63], True), ([0, 10, 63, 64], False), ([67, 68, 69, 70], False),
                    ([66, 72, 73], True), ([0, 66], True)]),
        video_case("largest-shards", 3, largest, MAX_SHARD_SIZE, [1], [([1], True)]),
    ]

    def malformed_video(name: str, payload: bytes, error: str) -> dict:
        return {"name": name, "hex": datagram(VIDEO_CHANNEL, UDP_CONNECTION, payload).hex(), "error": error}

    one = bytes(10)
    video_malformed = [
        malformed_video("short", shard_header(0, 10, 10, 0, 0, 1, 0)[:15], "Truncated"),
        malformed_video("reserved-set", shard_header(0, 10, 10, 0, 0, 1, 0, reserved=1) + one, "ReservedBitsSet"),
        malformed_video("empty-packet", shard_header(0, 0, 10, 0, 0, 1, 0) + one, "OutOfRange"),
        malformed_video("packet-too-big", shard_header(0, MAX_MEDIA_PACKET + 1, 1176, 0, 0, 64, 0) + one,
                        "OutOfRange"),
        malformed_video("empty-shards", shard_header(0, 10, 0, 0, 0, 1, 0) + one, "OutOfRange"),
        malformed_video("group-beyond", shard_header(0, 10, 10, 1, 0, 1, 0) + one, "OutOfRange"),
        malformed_video("wrong-k", shard_header(0, 10, 10, 0, 0, 2, 0) + one, "OutOfRange"),
        malformed_video("too-many-shards", shard_header(0, 64, 1, 0, 0, 64, 193) + bytes(1), "OutOfRange"),
        malformed_video("index-beyond", shard_header(0, 10, 10, 0, 2, 1, 1) + one, "OutOfRange"),
        malformed_video("data-short", shard_header(0, 10, 10, 0, 0, 1, 0) + bytes(9), "Truncated"),
        malformed_video("data-long", shard_header(0, 10, 10, 0, 0, 1, 0) + bytes(11), "TrailingBytes"),
        malformed_video("last-data-long", shard_header(0, 25, 10, 0, 2, 3, 1) + bytes(10), "TrailingBytes"),
        malformed_video("parity-short", shard_header(0, 25, 10, 0, 3, 3, 1) + bytes(5), "Truncated"),
    ]

    audio_packets = [media_bytes(2, 0, 1, 20_000 * i, 0, pattern_bytes(60 + i, 30 + i)) for i in range(3)]
    audio = {
        "packets": [p.hex() for p in audio_packets],
        "datagrams": [audio_datagram(0, audio_packets[0], None).hex(),
                      audio_datagram(1, audio_packets[1], audio_packets[0]).hex(),
                      audio_datagram(2, audio_packets[2], audio_packets[1]).hex()],
    }

    def malformed_audio(name: str, payload: bytes, error: str) -> dict:
        return {"name": name, "hex": datagram(AUDIO_CHANNEL, UDP_CONNECTION, payload).hex(), "error": error}

    entry = struct.pack(">H", 3) + b"abc"
    audio_malformed = [
        malformed_audio("short", struct.pack(">I", 1), "Truncated"),
        malformed_audio("count-three", struct.pack(">IB", 5, 3) + entry * 3, "OutOfRange"),
        malformed_audio("previous-of-first", struct.pack(">IB", 0, 2) + entry * 2, "OutOfRange"),
        malformed_audio("empty-packet", struct.pack(">IBH", 1, 1, 0), "OutOfRange"),
        malformed_audio("size-cut", struct.pack(">IB", 1, 1) + b"\x00", "Truncated"),
        malformed_audio("packet-cut", struct.pack(">IBH", 1, 1, 4) + b"abc", "Truncated"),
        malformed_audio("trailing", struct.pack(">IB", 1, 1) + entry + b"\x00", "TrailingBytes"),
    ]
    return [{
        "connection": UDP_CONNECTION,
        "maxShardSize": MAX_SHARD_SIZE,
        "open": {"video": datagram(VIDEO_CHANNEL, UDP_CONNECTION, b"").hex(),
                 "audio": datagram(AUDIO_CHANNEL, UDP_CONNECTION, b"").hex()},
        "closed": {"video": datagram(VIDEO_CHANNEL, UDP_CONNECTION, b"\x00").hex(),
                   "audio": datagram(AUDIO_CHANNEL, UDP_CONNECTION, b"\x00").hex()},
        "video": video,
        "videoMalformed": video_malformed,
        "audio": audio,
        "audioMalformed": audio_malformed,
    }]


def datagram_vectors() -> list[dict]:
    knock = datagram(DATAGRAM_HANDSHAKE_CHANNEL, 0, b"")
    server_hello = encode_frame("ServerHello", {"protocolVersion": PROTOCOL_VERSION, "nonce": UDP_NONCE.hex()})
    client_hello = encode_frame("ClientHello", {"protocolVersion": PROTOCOL_VERSION, "clientVersion": "2.0.0",
                                                "proof": udp_hello_proof(UDP_TOKEN, UDP_NONCE).hex()})
    tcp_proof = hmac.new(UDP_TOKEN, b"GSCP/1 hello" + UDP_NONCE, hashlib.sha256).digest()
    assert tcp_proof != udp_hello_proof(UDP_TOKEN, UDP_NONCE)
    first_body = control_body(1, 0, 0, [(0, bytes.fromhex("0401000000000000"))])
    second_body = control_body(2, 1, 0, [])
    headers = [
        {"name": "knock", "hex": knock.hex(), "channel": DATAGRAM_HANDSHAKE_CHANNEL, "connection": 0, "payloadSize": 0},
        {"name": "control-no-connection", "hex": datagram(0, 0, b"").hex(), "channel": 0, "connection": 0,
         "payloadSize": 0},
        {"name": "video-connection", "hex": datagram(1, 0xDEADBEEF, b"\x01\x02\x03").hex(), "channel": 1,
         "connection": 0xDEADBEEF, "payloadSize": 3},
        {"name": "audio-max-connection", "hex": datagram(2, 0xFFFFFFFF, b"\xff").hex(), "channel": 2,
         "connection": 0xFFFFFFFF, "payloadSize": 1},
        {"name": "bulk", "hex": datagram(3, 7, bytes(range(16))).hex(), "channel": 3, "connection": 7,
         "payloadSize": 16},
    ]
    malformed = [
        {"name": "short-header", "hex": knock[:7].hex(), "error": "Truncated"},
        {"name": "bad-magic", "hex": datagram(0, 0, b"").replace(b"GS", b"gs", 1).hex(), "error": "BadMagic"},
        {"name": "reserved-channel", "hex": datagram(9, 0, b"").hex(), "error": "InvalidEnum"},
        {"name": "flags-set", "hex": datagram(0, 0, b"", flags=1).hex(), "error": "ReservedBitsSet"},
        {"name": "too-long", "hex": datagram(1, 1, bytes(1273)).hex(), "error": "FrameTooLarge"},
    ]
    return [{
        "token": UDP_TOKEN.hex(),
        "nonce": UDP_NONCE.hex(),
        "helloProof": udp_hello_proof(UDP_TOKEN, UDP_NONCE).hex(),
        "tcpHelloProof": tcp_proof.hex(),
        "handshake": {
            "knock": knock.hex(),
            "serverHello": datagram(DATAGRAM_HANDSHAKE_CHANNEL, 0, server_hello).hex(),
            "clientHello": datagram(DATAGRAM_HANDSHAKE_CHANNEL, 0, client_hello).hex(),
            "accepted": datagram(DATAGRAM_HANDSHAKE_CHANNEL, UDP_CONNECTION, b"").hex(),
            "connection": UDP_CONNECTION,
        },
        "headers": headers,
        "malformed": malformed,
        "controlTags": [
            {"name": "counter-1", "body": first_body.hex(), "tag": control_tag(UDP_TOKEN, first_body).hex()},
            {"name": "counter-2", "body": second_body.hex(), "tag": control_tag(UDP_TOKEN, second_body).hex()},
            {"name": "empty-body", "body": b"".hex(), "tag": control_tag(UDP_TOKEN, b"").hex()},
        ],
    }]


def aes_multiply(a: int, b: int) -> int:
    product = 0
    while b:
        if b & 1:
            product ^= a
        a <<= 1
        if a & 0x100:
            a ^= 0x11B
        b >>= 1
    return product


def aes_sbox() -> list[int]:
    table = []
    for value in range(256):
        inverse = 0
        if value:
            inverse = 1
            for _ in range(254):
                inverse = aes_multiply(inverse, value)
        rotated = inverse
        out = inverse
        for _ in range(4):
            rotated = ((rotated << 1) | (rotated >> 7)) & 0xFF
            out ^= rotated
        table.append(out ^ 0x63)
    return table


AES_SBOX = aes_sbox()
assert (AES_SBOX[0x00], AES_SBOX[0x01], AES_SBOX[0x53], AES_SBOX[0xFF]) == (0x63, 0x7C, 0xED, 0x16)


def aes_round_keys(key: bytes) -> list[list[int]]:
    words_in_key = len(key) // 4
    rounds = words_in_key + 6
    words = [list(key[4 * i:4 * i + 4]) for i in range(words_in_key)]
    round_constant = 1
    for i in range(words_in_key, 4 * (rounds + 1)):
        word = list(words[i - 1])
        if i % words_in_key == 0:
            word = [AES_SBOX[b] for b in word[1:] + word[:1]]
            word[0] ^= round_constant
            round_constant = aes_multiply(round_constant, 2)
        elif words_in_key > 6 and i % words_in_key == 4:
            word = [AES_SBOX[b] for b in word]
        words.append([a ^ b for a, b in zip(words[i - words_in_key], word)])
    return [sum(words[4 * r:4 * r + 4], []) for r in range(rounds + 1)]


def aes_encrypt_block(round_keys: list[list[int]], block: bytes) -> bytes:
    state = [b ^ k for b, k in zip(block, round_keys[0])]
    last = len(round_keys) - 1
    for number in range(1, last + 1):
        state = [AES_SBOX[b] for b in state]
        state = [state[((column + row) % 4) * 4 + row] for column in range(4) for row in range(4)]
        if number != last:
            mixed = []
            for column in range(4):
                a = state[4 * column:4 * column + 4]
                mixed += [aes_multiply(a[0], 2) ^ aes_multiply(a[1], 3) ^ a[2] ^ a[3],
                          a[0] ^ aes_multiply(a[1], 2) ^ aes_multiply(a[2], 3) ^ a[3],
                          a[0] ^ a[1] ^ aes_multiply(a[2], 2) ^ aes_multiply(a[3], 3),
                          aes_multiply(a[0], 3) ^ a[1] ^ a[2] ^ aes_multiply(a[3], 2)]
            state = mixed
        state = [b ^ k for b, k in zip(state, round_keys[number])]
    return bytes(state)


def gcm_multiply(x: int, y: int) -> int:
    product = 0
    value = y
    for bit in range(127, -1, -1):
        if (x >> bit) & 1:
            product ^= value
        value = (value >> 1) ^ (0xE1 << 120) if value & 1 else value >> 1
    return product


def gcm_hash(subkey: int, aad: bytes, ciphertext: bytes) -> int:
    def blocks(data: bytes):
        for start in range(0, len(data), 16):
            chunk = data[start:start + 16]
            yield int.from_bytes(chunk + bytes(16 - len(chunk)), "big")

    digest = 0
    for block in list(blocks(aad)) + list(blocks(ciphertext)):
        digest = gcm_multiply(digest ^ block, subkey)
    return gcm_multiply(digest ^ ((len(aad) * 8) << 64 | len(ciphertext) * 8), subkey)


def aes_gcm_seal(key: bytes, nonce: bytes, plaintext: bytes, aad: bytes) -> bytes:
    assert len(key) in (16, 32) and len(nonce) == 12
    round_keys = aes_round_keys(key)
    subkey = int.from_bytes(aes_encrypt_block(round_keys, bytes(16)), "big")
    ciphertext = bytearray()
    for index, start in enumerate(range(0, len(plaintext), 16)):
        stream = aes_encrypt_block(round_keys, nonce + struct.pack(">I", index + 2))
        ciphertext += bytes(a ^ b for a, b in zip(plaintext[start:start + 16], stream))
    mask = int.from_bytes(aes_encrypt_block(round_keys, nonce + struct.pack(">I", 1)), "big")
    tag = (gcm_hash(subkey, aad, bytes(ciphertext)) ^ mask).to_bytes(16, "big")
    return bytes(ciphertext) + tag


GCM_KNOWN_ANSWERS = [
    {"name": "gcm-spec-13", "key": "00" * 32, "nonce": "00" * 12, "aad": "", "plaintext": "",
     "ciphertext": "", "tag": "530f8afbc74536b9a963b4f1c4cb738b"},
    {"name": "gcm-spec-14", "key": "00" * 32, "nonce": "00" * 12, "aad": "", "plaintext": "00" * 16,
     "ciphertext": "cea7403d4d606b6e074ec5d3baf39d18", "tag": "d0d1c8a799996bf0265b98b5d48ab919"},
    {"name": "gcm-spec-15", "key": "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
     "nonce": "cafebabefacedbaddecaf888", "aad": "",
     "plaintext": "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                  "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
     "ciphertext": "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
                   "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad",
     "tag": "b094dac5d93471bdec1a502270e3cc6c"},
    {"name": "gcm-spec-16", "key": "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
     "nonce": "cafebabefacedbaddecaf888", "aad": "feedfacedeadbeeffeedfacedeadbeefabaddad2",
     "plaintext": "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                  "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
     "ciphertext": "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
                   "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662",
     "tag": "76fc6ece0f4e1768cddf8853bb2d551b"},
]
for _known in GCM_KNOWN_ANSWERS:
    _sealed = aes_gcm_seal(bytes.fromhex(_known["key"]), bytes.fromhex(_known["nonce"]),
                           bytes.fromhex(_known["plaintext"]), bytes.fromhex(_known["aad"]))
    assert _sealed.hex() == _known["ciphertext"] + _known["tag"], _known["name"]

LAN_MAGIC = b"GSCL"
LAN_VERSION = 1
LAN_CONTEXT = b"GSCP/1 companion lan"
LAN_STATUS = ["accepted", "unknown", "versionMismatch"]
LAN_DIRECTION = {"toCompanion": 0, "toComputer": 1}
LAN_TAG_SIZE = 16


def base64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def lan_hello(token: str, phone: bytes, nonce: bytes, version: int = LAN_VERSION, magic: bytes = LAN_MAGIC) -> bytes:
    data = token.encode("ascii")
    assert len(phone) == 16 and len(nonce) == 16
    return magic + struct.pack(">BB", version, len(data)) + data + phone + nonce


def lan_reply(status: str, pc: bytes, nonce: bytes, version: int = LAN_VERSION) -> bytes:
    assert len(pc) == 16 and len(nonce) == 16
    return LAN_MAGIC + struct.pack(">BB", version, LAN_STATUS.index(status)) + pc + nonce


def lan_session_key(key: bytes, hello: bytes, reply: bytes) -> bytes:
    return hmac.new(key, LAN_CONTEXT + hello + reply, hashlib.sha256).digest()


def lan_record(session: bytes, direction: str, counter: int, frame: bytes) -> bytes:
    prefix = struct.pack(">I", len(frame) + LAN_TAG_SIZE)
    nonce = struct.pack(">IQ", LAN_DIRECTION[direction], counter)
    return prefix + aes_gcm_seal(session, nonce, frame, prefix)


def companion_lan_vectors() -> list[dict]:
    key = bytes((0x30 + 7 * i) & 0xFF for i in range(32))
    token = base64url(bytes(range(0xA0, 0xB0)))
    phone = bytes.fromhex("5f1e0c9a3b7d42e8a6c1f09d8e7b6a51")
    pc = bytes.fromhex("c0ffee00112233445566778899aabbcc")
    assert len(token) == 22

    def handshake(name: str, hello_token: str, phone_nonce: bytes, pc_nonce: bytes, exchange: list) -> dict:
        hello = lan_hello(hello_token, phone, phone_nonce)
        reply = lan_reply("accepted", pc, pc_nonce)
        session = lan_session_key(key, hello, reply)
        counters = {"toCompanion": 0, "toComputer": 0}
        records = []
        for direction, message_name, fields in exchange:
            frame = encode_frame(message_name, fields)
            records.append({"direction": direction, "counter": counters[direction], "message": message_name,
                            "frame": frame.hex(), "hex": lan_record(session, direction, counters[direction],
                                                                   frame).hex()})
            counters[direction] += 1
        return {"name": name, "token": hello_token, "phoneNonce": phone_nonce.hex(), "pcNonce": pc_nonce.hex(),
                "status": "accepted", "hello": hello.hex(), "reply": reply.hex(), "sessionKey": session.hex(),
                "records": records}

    pairing = handshake("pairing", token, pattern_bytes(16, 3), pattern_bytes(16, 9), [
        ("toCompanion", "CompanionHello", {"version": 1, "token": ""}),
        ("toComputer", "CompanionWelcome",
         {"version": 1, "overlay": "granted", "width": 1080, "height": 2400, "rotation": 0}),
        ("toComputer", "InputBoxChanged",
         {"shown": True, "multiLine": False, "inputType": 0x00004001, "action": "send"}),
        ("toCompanion", "CommitText", {"text": "你好"}),
        ("toCompanion", "EditorAction", {"action": "unspecified"}),
        ("toCompanion", "KeyHintState", {"shown": True, "faded": True, "opacity": 60}),
    ])
    reconnect = handshake("reconnect", "", pattern_bytes(16, 41), pattern_bytes(16, 77), [
        ("toCompanion", "CompanionHello", {"version": 1, "token": ""}),
        ("toComputer", "CompanionWelcome",
         {"version": 1, "overlay": "noPermission", "width": 0, "height": 0, "rotation": 1}),
        ("toCompanion", "Ping", {"nonce": 1, "clientTimeUs": "5000000"}),
        ("toComputer", "Pong", {"nonce": 1, "clientTimeUs": "5000000", "serverTimeUs": "7000000"}),
    ])
    refused_hello = lan_hello("", phone, pattern_bytes(16, 5))
    refused = {"name": "refused", "token": "", "status": "unknown", "hello": refused_hello.hex(),
               "reply": lan_reply("unknown", bytes(16), bytes(16)).hex()}
    malformed_hellos = [
        {"name": "short", "hex": lan_hello(token, phone, bytes(16))[:-1].hex(), "error": "Truncated"},
        {"name": "bad-magic", "hex": lan_hello(token, phone, bytes(16), magic=b"GET ").hex(), "error": "BadMagic"},
        {"name": "other-version", "hex": lan_hello(token, phone, bytes(16), version=2).hex(),
         "error": "VersionMismatch"},
        {"name": "token-too-long", "hex": (LAN_MAGIC + struct.pack(">BB", 1, 65) + b"t" * 65 + bytes(32)).hex(),
         "error": "LengthExceeded"},
    ]
    malformed_replies = [
        {"name": "short", "hex": lan_reply("accepted", pc, bytes(16))[:-1].hex(), "error": "Truncated"},
        {"name": "bad-magic", "hex": (b"HTTP" + lan_reply("accepted", pc, bytes(16))[4:]).hex(), "error": "BadMagic"},
        {"name": "other-version", "hex": lan_reply("accepted", pc, bytes(16), version=3).hex(),
         "error": "VersionMismatch"},
        {"name": "invalid-status", "hex": (LAN_MAGIC + struct.pack(">BB", 1, 3) + bytes(32)).hex(),
         "error": "InvalidEnum"},
    ]
    session = bytes.fromhex(pairing["sessionKey"])
    commit = pairing["records"][3]
    assert commit["message"] == "CommitText"
    tampered = bytearray(bytes.fromhex(commit["hex"]))
    tampered[-1] ^= 0x01
    wrong_counter = lan_record(session, "toCompanion", 7, encode_frame("CommitText", {"text": "x"}))
    malformed_records = [
        {"name": "tag-tampered", "direction": "toCompanion", "counter": commit["counter"],
         "hex": bytes(tampered).hex(), "error": "BadTag"},
        {"name": "wrong-counter", "direction": "toCompanion", "counter": 0, "hex": wrong_counter.hex(),
         "error": "BadTag"},
        {"name": "too-short", "direction": "toCompanion", "counter": 0,
         "hex": (struct.pack(">I", 21) + bytes(21)).hex(), "error": "OutOfRange"},
        {"name": "too-long", "direction": "toCompanion", "counter": 0,
         "hex": struct.pack(">I", 6 + COMPANION_MAX + LAN_TAG_SIZE + 1).hex(), "error": "OutOfRange"},
    ]
    zero_token = base64url(bytes(16))
    ff_key = bytes([0xFF]) * 32
    qr = [
        {"host": "192.168.1.23", "port": 27185, "token": token, "key": key.hex(),
         "url": f"http://192.168.1.23:27185/c/{token}#k={base64url(key)}", "target": f"/c/{token}"},
        {"host": "10.0.0.2", "port": 50123, "token": zero_token, "key": ff_key.hex(),
         "url": f"http://10.0.0.2:50123/c/{zero_token}#k={base64url(ff_key)}", "target": f"/c/{zero_token}"},
    ]
    return [{
        "key": key.hex(),
        "phone": phone.hex(),
        "pc": pc.hex(),
        "context": LAN_CONTEXT.decode("ascii"),
        "handshakes": [pairing, reconnect, refused],
        "malformedHellos": malformed_hellos,
        "malformedReplies": malformed_replies,
        "malformedRecords": malformed_records,
        "qr": qr,
        "gcm": GCM_KNOWN_ANSWERS,
    }]


FAMILIES: dict[str, Callable[[], list[dict]]] = {
    "handshake": handshake_vectors,
    "input": input_vectors,
    "media-config": media_config_vectors,
    "device": device_vectors,
    "maintenance": maintenance_vectors,
    "events": event_vectors,
    "bulk": bulk_vectors,
    "companion": companion_vectors,
    "media": media_vectors,
    "malformed": malformed_vectors,
}


def main() -> None:
    out_dir = Path(__file__).resolve().parent.parent / "vectors"
    covered: set[str] = set()
    total = 0
    for family, build in FAMILIES.items():
        vectors = build()
        covered.update(v["message"] for v in vectors if "message" in v)
        total += len(vectors)
        document = {"format": "gamescrcpy.protocol-vectors",
                    "protocolVersion": PROTOCOL_VERSION, "family": family, "vectors": vectors}
        text = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
        (out_dir / f"{family}.json").write_text(text, encoding="utf-8", newline="\n")
    expected = set(MESSAGES) | {"VideoSession", "AudioSession", "MediaConfig", "MediaFrame"}
    missing = sorted(expected - covered)
    assert not missing, f"message types without vectors: {missing}"
    print(f"wrote {total} vectors covering {len(covered)} message types to {out_dir}")

    fec = fec_vectors()
    (out_dir / "fec").mkdir(exist_ok=True)
    document = {"format": "gamescrcpy.fec-vectors", "spec": "10.4", "vectors": fec}
    text = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    (out_dir / "fec" / "reed-solomon.json").write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {len(fec)} Reed-Solomon vectors to {out_dir / 'fec'}")

    datagrams = datagram_vectors()
    (out_dir / "direct").mkdir(exist_ok=True)
    document = {"format": "gamescrcpy.datagram-vectors", "spec": "10.2", "vectors": datagrams}
    text = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    (out_dir / "direct" / "datagrams.json").write_text(text, encoding="utf-8", newline="\n")
    reliable = reliable_vectors()
    document = {"format": "gamescrcpy.reliable-vectors", "spec": "10.3", "vectors": reliable}
    text = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    (out_dir / "direct" / "reliable.json").write_text(text, encoding="utf-8", newline="\n")
    media = direct_media_vectors()
    document = {"format": "gamescrcpy.direct-media-vectors", "spec": "10.5-10.6", "vectors": media}
    text = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    (out_dir / "direct" / "media.json").write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote the WiFi direct datagram, reliable channel and media vectors to {out_dir / 'direct'}")

    lan = companion_lan_vectors()
    (out_dir / "companion").mkdir(exist_ok=True)
    document = {"format": "gamescrcpy.companion-lan-vectors", "spec": "11.7", "vectors": lan}
    text = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    (out_dir / "companion" / "lan.json").write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote the companion LAN handshake, record and AES-256-GCM vectors to {out_dir / 'companion'}")


if __name__ == "__main__":
    main()
