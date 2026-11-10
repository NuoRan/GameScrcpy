#!/usr/bin/env python3
"""Smoke test of the device server on a real device or emulator (SPEC chapters 2, 3, 4 and 7).

Pushes the release server, launches it through app_process with the token on standard input,
waits for GS_READY, performs the HMAC handshake over adb forward, exchanges a ping and ends the
session with Goodbye. With --video it also opens the video channel, streams for a while, checks
the packet order and clock, writes the elementary stream and lets ffprobe decode it. Standard
library only.

    python tools/server_smoke.py [--serial emulator-5556] [--apk android/server/build/outputs/apk/release/server-release-unsigned.apk]
                                 [--video SECONDS [--codec h264|h265|av1] [--max-size N] [--bitrate BPS]
                                  [--record PATH] [--ffprobe PATH]] [--foreground]
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import html
import os
import re
import secrets
import select
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

MAGIC = 0x47534350
PROTOCOL_VERSION = 1
REMOTE_JAR = "/data/local/tmp/gs-server-smoke.jar"


def find_adb() -> str:
    sdk = (os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
           or os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk"))
    candidate = Path(sdk) / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb")
    return str(candidate) if candidate.exists() else (shutil.which("adb") or "adb")


def frame(type_id: int, payload: bytes) -> bytes:
    return struct.pack(">HI", type_id, len(payload)) + payload


def read_exactly(sock: socket.socket, size: int) -> bytes:
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed by the server")
        data += chunk
    return data


def read_frame(sock: socket.socket) -> tuple[int, bytes]:
    type_id, length = struct.unpack(">HI", read_exactly(sock, 6))
    return type_id, read_exactly(sock, length)


def read_str8(payload: bytes, offset: int) -> tuple[str, int]:
    length = payload[offset]
    return payload[offset + 1:offset + 1 + length].decode("utf-8"), offset + 1 + length


DISPLAY = struct.Struct(">IHHBHIHHHH")
CODECS = {1: "h264", 2: "h265", 3: "av1"}
FOREGROUND_APP = 0x8304
CAPABILITY_FOREGROUND_APP = 1 << 7


def parse_foreground(payload: bytes) -> tuple[str, str, bool]:
    package, offset = read_str8(payload, 0)
    activity, offset = read_str8(payload, offset)
    assert offset + 1 == len(payload), f"{len(payload) - offset - 1} extra bytes in ForegroundAppChanged"
    flags = payload[offset]
    assert flags & ~0x01 == 0, f"reserved flags {flags:#04x} in ForegroundAppChanged"
    return package, activity, bool(flags & 0x01)


def note_foreground(foreground: list[tuple[str, str, bool]], payload: bytes) -> None:
    foreground.append(parse_foreground(payload))
    package, activity, game = foreground[-1]
    print(f"  foreground app: {package or '(none)'} / {activity or '(none)'}{' (game)' if game else ''}")


def read_reply(sock: socket.socket, wanted: int, foreground: list[tuple[str, str, bool]]) -> bytes:
    while True:
        type_id, payload = read_frame(sock)
        if type_id == wanted:
            return payload
        assert type_id == FOREGROUND_APP, f"expected {wanted:#06x}, got {type_id:#06x}: {payload!r}"
        note_foreground(foreground, payload)


def parse_welcome(payload: bytes) -> dict:
    offset = 0
    fields: dict = {}
    for name in ("serverVersion", "manufacturer", "model", "androidRelease"):
        fields[name], offset = read_str8(payload, offset)
    fields["sdkInt"] = struct.unpack_from(">H", payload, offset)[0]
    fields["serialNo"], offset = read_str8(payload, offset + 2)
    displays = []
    for _ in range(payload[offset]):
        (display_id, width, height, rotation, dpi, refresh, left, top, right,
         bottom) = DISPLAY.unpack_from(payload, offset + 1)
        displays.append({"id": display_id, "size": f"{width}x{height}", "rotation": rotation, "dpi": dpi,
                         "refreshHz": refresh / 1000, "safe": (left, top, right, bottom)})
        offset += DISPLAY.size
    offset += 1
    encoders = []
    for _ in range(payload[offset]):
        codec = CODECS.get(payload[offset + 1], payload[offset + 1])
        name, offset = read_str8(payload, offset + 2)
        encoders.append(f"{codec}:{name}{'' if payload[offset] else ' (software)'}")
    offset += 1
    fields["displays"] = displays
    fields["encoders"] = encoders
    fields["audioStatus"], fields["capabilities"], fields["resumed"] = struct.unpack_from(">BIB", payload, offset)
    assert offset + 6 == len(payload), f"{len(payload) - offset - 6} trailing bytes in Welcome"
    return fields


MEDIA_HEADER = struct.Struct(">BBHqII")
VIDEO_SESSION = struct.Struct(">BHHIHHB")
CODEC_IDS = {name: value for value, name in CODECS.items()}


def open_channel(port: int, token: bytes, channel: int, receive_buffer: int = 0) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if receive_buffer:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
    sock.settimeout(5)
    sock.connect(("127.0.0.1", port))
    type_id, payload = read_frame(sock)
    assert type_id == 0x8001, f"expected ServerHello, got {type_id:#06x}"
    nonce = payload[6:22]
    proof = hmac.new(token, b"GSCP/1 channel" + nonce + bytes([channel]), hashlib.sha256).digest()
    sock.sendall(frame(0x0005, bytes([channel]) + proof))
    type_id, payload = read_frame(sock)
    assert type_id == 0x8006 and payload == bytes([channel]), \
        f"expected ChannelAccepted, got {type_id:#06x} {payload!r}"
    return sock


AUDIO_SESSION = struct.Struct(">BIB")


def stream_audio(port: int, token: bytes, control: socket.socket, seconds: float) -> bool:
    reader = ControlReader(control)
    reader.start()
    control = LockedSender(control)
    beating = threading.Event()
    beating.set()

    def heartbeat_loop() -> None:
        try:
            while beating.is_set():
                control.sendall(frame(0x0401, struct.pack(">IQ", 9, time.monotonic_ns() // 1000)))
                time.sleep(1.0)
        except OSError:
            return

    threading.Thread(target=heartbeat_loop, daemon=True).start()
    audio = open_channel(port, token, 2)
    audio.settimeout(10)
    control.sendall(frame(0x0211, struct.pack(">?I", True, 128_000)))
    sessions, config, pts, delays, size_total = [], None, [], [], 0
    started = time.monotonic()
    try:
        while time.monotonic() - started < seconds:
            kind, _flags, session, pts_us, delay_us, size = MEDIA_HEADER.unpack(
                read_exactly(audio, MEDIA_HEADER.size))
            payload = read_exactly(audio, size)
            if kind == 0:
                sessions.append((session, AUDIO_SESSION.unpack(payload)))
            elif kind == 1:
                config = payload
            else:
                pts.append(pts_us)
                delays.append(delay_us)
                size_total += size
    except socket.timeout:
        print("  no audio packet for 10 s")
    control.sendall(frame(0x0211, struct.pack(">?I", False, 0)))
    beating.clear()
    audio.close()
    gaps = sorted(b - a for a, b in zip(pts, pts[1:]))
    spacing = gaps[len(gaps) // 2] / 1000 if gaps else 0
    encode = sorted(delays)[len(delays) // 2] / 1000 if delays else 0
    head = config[:8] if config else None
    print(f"  audio sessions {sessions}; config {head!r} ({len(config) if config else 0} bytes); {len(pts)} packets, "
          f"{size_total * 8 / seconds / 1000:.0f} kbit/s; packet spacing median {spacing:.1f} ms; "
          f"capture to encoder output median {encode:.1f} ms")
    return (bool(sessions) and sessions[0][1] == (1, 48_000, 2) and config is not None
            and config.startswith(b"OpusHead") and len(pts) >= seconds * 40
            and all(b >= a for a, b in zip(pts, pts[1:])) and all(delays))


def configure_video(codec: str, max_size: int, max_fps: int, bitrate: int) -> bytes:
    return frame(0x0201, struct.pack(">BIHHHBHHIH", 0, 0, 0, 0, 0, CODEC_IDS[codec], max_size, max_fps, bitrate, 0))


class ControlReader(threading.Thread):
    def __init__(self, sock: socket.socket):
        super().__init__(daemon=True)
        self.sock = sock
        self.notices: list[str] = []
        self.pongs: list[tuple[int, int]] = []
        self.display_changes: list[tuple] = []
        self.lock = threading.Lock()
        self.best_rtt_us = None
        self.offset_us = None

    def run(self) -> None:
        try:
            while True:
                type_id, payload = read_frame(self.sock)
                if type_id == 0x8401:
                    _, client_time, server_time = struct.unpack(">IQQ", payload)
                    now = time.monotonic_ns() // 1000
                    with self.lock:
                        self.pongs.append((now, server_time))
                        rtt = now - client_time
                        if self.best_rtt_us is None or rtt < self.best_rtt_us:
                            self.best_rtt_us = rtt
                            self.offset_us = server_time - (client_time + now) // 2
                elif type_id == 0x8403:
                    level, code = struct.unpack_from(">BH", payload)
                    length = struct.unpack_from(">H", payload, 3)[0]
                    text = payload[5:5 + length].decode("utf-8")
                    print(f"  notice level={level} code={code}: {text}")
                    if level == 2:
                        with self.lock:
                            self.notices.append(text)
                elif type_id == 0x8303:
                    change = DISPLAY.unpack(payload)
                    print(f"  display changed: {change}")
                    with self.lock:
                        self.display_changes.append(change)
        except (ConnectionError, OSError):
            return


class VideoReader(threading.Thread):
    def __init__(self, sock: socket.socket):
        super().__init__(daemon=True)
        self.sock = sock
        self.frames = 0
        self.session = None
        self.display = None
        self.changed = threading.Condition()

    def run(self) -> None:
        try:
            while True:
                kind, _, session, _, _, size = MEDIA_HEADER.unpack(read_exactly(self.sock, MEDIA_HEADER.size))
                payload = read_exactly(self.sock, size)
                with self.changed:
                    if kind == 0:
                        info = VIDEO_SESSION.unpack(payload)
                        self.session = (session, info[1], info[2])
                        self.display = (info[4], info[5])
                    elif kind == 2:
                        self.frames += 1
                    self.changed.notify_all()
        except (ConnectionError, OSError):
            return

    def wait_for_frames(self, count: int, timeout: float) -> bool:
        with self.changed:
            return self.changed.wait_for(lambda: self.frames >= count, timeout)

    def wait_quiet(self, quiet: float, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        with self.changed:
            while time.monotonic() < deadline:
                seen = self.frames
                if not self.changed.wait_for(lambda: self.frames != seen, quiet):
                    return True
        return False


class LockedSender:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.lock = threading.Lock()

    def sendall(self, data: bytes) -> None:
        with self.lock:
            self.sock.sendall(data)

    def shutdown(self, how: int) -> None:
        with self.lock:
            self.sock.shutdown(how)


FULL = 0xFFFF


def at(fraction: float) -> int:
    return round(fraction * FULL)


def last_pointer(serial: str) -> tuple[float, float] | None:
    for line in reversed(adb_shell(serial, "dumpsys input").splitlines()):
        if "MotionEvent(" not in line:
            continue
        match = re.search(r"\b0: \((-?[\d.]+), (-?[\d.]+)\)", line) or re.search(
            r"x\[0\]=(-?[\d.]+), y\[0\]=(-?[\d.]+)", line)
        if match:
            return float(match.group(1)), float(match.group(2))
    return None


def touch_frame(session: int, points: list[tuple[int, int, int, int]]) -> bytes:
    body = b"".join(struct.pack(">BBHHH", action, pid, x, y, 0 if action in (1, 3) else 0xFFFF)
                    for action, pid, x, y in points)
    return frame(0x0101, struct.pack(">HB", session, len(points)) + body)


def adb_shell(serial: str, command: str) -> str:
    return subprocess.run([find_adb(), "-s", serial, "shell", command], capture_output=True, encoding="utf-8",
                          errors="replace").stdout or ""


def poll(condition, timeout: float, interval: float = 0.1) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(interval)
    return condition()


def touch_states(serial: str) -> str:
    lines = adb_shell(serial, "dumpsys input").splitlines()
    touching = next((l for l in lines if "touchingPointers=[Pointer" in l), "")
    if touching:
        return touching.strip()
    line = next((l for l in lines if "TouchStates:" in l), "")
    return line.strip()


def exercise_input(port: int, token: bytes, control: socket.socket, args) -> bool:
    serial = args.serial
    adb_shell(serial, "input keyevent KEYCODE_WAKEUP; am start -W -a android.settings.SETTINGS")
    reader = ControlReader(control)
    reader.start()
    video = open_channel(port, token, 1)
    video.settimeout(None)
    frames = VideoReader(video)
    frames.start()
    control = LockedSender(control)
    control.sendall(configure_video("h264", 0, 0, 8_000_000))
    ok = True
    beating = threading.Event()
    beating.set()

    def heartbeat_loop() -> None:
        try:
            while beating.is_set():
                control.sendall(frame(0x0401, struct.pack(">IQ", 7, time.monotonic_ns() // 1000)))
                time.sleep(1.0)
        except OSError:
            return

    threading.Thread(target=heartbeat_loop, daemon=True).start()

    def heartbeat() -> None:
        pass

    def check(name: str, passed: bool, detail: str = "") -> None:
        nonlocal ok
        ok = ok and passed
        print(f"  {'ok  ' if passed else 'FAIL'} {name}{': ' + detail if detail else ''}")

    assert frames.wait_for_frames(1, 10), "no first frame"
    session, width, height = frames.session

    def frames_during(seconds: float, action) -> int:
        before = frames.frames
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            action()
            heartbeat()
        time.sleep(0.4)
        return frames.frames - before

    frames.wait_quiet(1.0, 8)
    idle = frames_during(1.5, lambda: time.sleep(0.25))
    upward = [True]

    def swipe() -> None:
        x = at(0.5)
        start, end = (at(0.75), at(0.35)) if upward[0] else (at(0.35), at(0.75))
        upward[0] = not upward[0]
        control.sendall(touch_frame(session, [(0, 0, x, start)]))
        for step in range(1, 11):
            time.sleep(0.025)
            control.sendall(touch_frame(session, [(2, 0, x, start + (end - start) * step // 10)]))
        control.sendall(touch_frame(session, [(1, 0, x, end)]))
        time.sleep(0.1)

    touched = frames_during(2.5, swipe)
    check("touch swipes scroll the list", touched > max(10, 3 * idle), f"{touched} frames vs {idle} while idle")

    display_width, display_height = frames.display
    control.sendall(touch_frame(session, [(0, 0, at(0.25), at(0.5))]))
    poll(lambda: "<no displays touched>" not in touch_states(serial), 2)
    landed = last_pointer(serial)
    control.sendall(touch_frame(session, [(1, 0, at(0.25), at(0.5))]))
    poll(lambda: "<no displays touched>" in touch_states(serial), 2)
    aimed = (display_width * 0.25, display_height * 0.5)
    check("a touch lands where it was aimed",
          landed is not None and abs(landed[0] - aimed[0]) <= 2 and abs(landed[1] - aimed[1]) <= 2,
          f"at {landed}, aimed at {aimed} on a {display_width}x{display_height} display")

    fingers = [(0, pid, FULL * (pid + 1) // 12, FULL // 3) for pid in range(10)]
    control.sendall(touch_frame(session, fingers))
    for step in range(1, 6):
        time.sleep(0.02)
        control.sendall(touch_frame(session, [(2, pid, x, y + step * FULL // 25) for _, pid, x, y in fingers]))
    held = poll(lambda: "<no displays touched>" not in touch_states(serial), 2)
    recent = [l.strip() for l in adb_shell(serial, "dumpsys input").splitlines() if "MotionEvent(" in l]
    ten = [l for l in recent if l.count(": (") == 10 or "pointerCount=10" in l]
    check("ten fingers down at once", held and bool(ten), f"{len(ten)} recent events with 10 pointers")
    control.sendall(touch_frame(session, [(1, pid, x, y + 5 * FULL // 25) for _, pid, x, y in fingers]))
    check("all ten lifted", poll(lambda: "<no displays touched>" in touch_states(serial), 2), touch_states(serial))
    frames.wait_quiet(0.8, 5)
    idle = frames_during(1.0, lambda: time.sleep(0.25))

    def scroll() -> None:
        control.sendall(frame(0x0104, struct.pack(">HHHhh", at(0.5), at(0.5), session, 0, -3 * 256)))
        time.sleep(0.15)

    scrolled = frames_during(2.0, scroll)
    check("mouse wheel scrolls", scrolled > max(5, 2 * idle), f"{scrolled} frames vs {idle} while idle")

    def focus() -> str:
        return " | ".join(l.strip() for l in adb_shell(serial, "dumpsys window").splitlines() if "mCurrentFocus=" in l)

    settings = "com.android.settings/"
    adb_shell(serial, "am start -W -a android.settings.SETTINGS")
    in_settings = poll(lambda: settings in focus(), 3)
    control.sendall(frame(0x0102, struct.pack(">BIIH", 0, 4, 0, 0)) + frame(0x0102, struct.pack(">BIIH", 1, 4, 0, 0)))
    left = poll(lambda: settings not in focus() and "Window{" in focus(), 3)
    check("BACK key leaves Settings", in_settings and left, focus())

    adb_shell(serial, "input keyevent KEYCODE_SLEEP")
    asleep = poll(lambda: "mWakefulness=Asleep" in adb_shell(serial, "dumpsys power"), 3)
    control.sendall(frame(0x0105, bytes([0])) + frame(0x0105, bytes([1])))
    awake = poll(lambda: "mWakefulness=Awake" in adb_shell(serial, "dumpsys power"), 3)
    check("BackOrScreenOn wakes a dark screen", asleep and awake)

    if args.rotate:
        adb_shell(serial, "am start -W -a android.settings.SETTINGS")
        old_session = frames.session[0]
        set_rotation(serial, 1)
        rotated = poll(lambda: frames.session[0] != old_session, 5)
        session, width, height = frames.session
        check("rotation starts a landscape session", rotated and width > height, str(frames.session))
        frames.wait_quiet(1.0, 6)
        stale = frames_during(1.5, lambda: (control.sendall(touch_frame(old_session, [(0, 0, at(0.5), at(0.7))])),
                                            control.sendall(touch_frame(old_session, [(2, 0, at(0.5), at(0.3))])),
                                            control.sendall(touch_frame(old_session, [(1, 0, at(0.5), at(0.3))])),
                                            time.sleep(0.2)))
        landscape = frames_during(2.5, swipe)
        check("landscape swipes scroll, stale-session touches do not", landscape > max(10, 3 * stale),
              f"{landscape} frames vs {stale} for the stale session")
        set_rotation(serial, 0)
        poll(lambda: frames.session[1] < frames.session[2], 5)
        session, width, height = frames.session

    adb_shell(serial, "am start -W -a android.settings.SETTINGS")
    control.sendall(touch_frame(session, [(0, 3, FULL // 3, FULL // 2), (0, 7, 2 * FULL // 3, FULL // 2)]))
    poll(lambda: "<no displays touched>" not in touch_states(serial), 2)
    beating.clear()
    control.shutdown(socket.SHUT_RDWR)
    video.close()
    released = poll(lambda: "<no displays touched>" in touch_states(serial), 3)
    check("a dropped connection leaves no finger pressed", released, touch_states(serial))
    with reader.lock:
        errors = list(reader.notices)
    check("no error notices", not errors, "; ".join(errors))
    return ok


def edit_text(serial: str) -> str | None:
    xml = adb_shell(serial, "uiautomator dump /sdcard/gs_ui.xml >/dev/null 2>&1; cat /sdcard/gs_ui.xml")
    for node in re.findall(r"<node [^>]*>", xml):
        if 'class="android.widget.EditText"' in node and 'focused="true"' in node:
            match = re.search(r' text="([^"]*)"', node)
            return html.unescape(match.group(1)) if match else ""
    return None


def key_press(keycode: int, meta: int = 0) -> bytes:
    return (frame(0x0102, struct.pack(">BIIH", 0, keycode, meta, 0))
            + frame(0x0102, struct.pack(">BIIH", 1, keycode, meta, 0)))


def text_message(text: str) -> bytes:
    data = text.encode("utf-8")
    return frame(0x0103, struct.pack(">H", len(data)) + data)


def exercise_text(port: int, token: bytes, control: socket.socket, args) -> bool:
    serial = args.serial
    reader = ControlReader(control)
    reader.start()
    control = LockedSender(control)
    video = open_channel(port, token, 1)
    video.settimeout(None)
    frames = VideoReader(video)
    frames.start()
    control.sendall(configure_video("h264", 0, 0, 8_000_000))
    assert frames.wait_for_frames(1, 10), "no first frame"
    beating = threading.Event()
    beating.set()

    def heartbeat_loop() -> None:
        try:
            while beating.is_set():
                control.sendall(frame(0x0401, struct.pack(">IQ", 7, time.monotonic_ns() // 1000)))
                time.sleep(1.0)
        except OSError:
            return

    threading.Thread(target=heartbeat_loop, daemon=True).start()
    ok = True

    def check(name: str, passed: bool, detail: str = "") -> None:
        nonlocal ok
        ok = ok and passed
        print(f"  {'ok  ' if passed else 'FAIL'} {name}{': ' + detail if detail else ''}")

    def field_becomes(expected: str) -> bool:
        return poll(lambda: edit_text(serial) == expected, 8, 0.2)

    adb_shell(serial, "input keyevent KEYCODE_WAKEUP; am start -S -W -a com.android.settings.action.SETTINGS_SEARCH")
    check("a text field has focus", poll(lambda: edit_text(serial) is not None, 5))

    control.sendall(key_press(29, 0x3000) + key_press(67))
    control.sendall(text_message("ORIGINAL"))
    typed = field_becomes("ORIGINAL")
    control.sendall(key_press(29, 0x3000) + key_press(278) + key_press(67))
    check("keyboard text types as keys", typed and poll(lambda: edit_text(serial) != "ORIGINAL", 5))

    mixed = "Hi 你好，世界 123"
    control.sendall(text_message(mixed))
    check("mixed text arrives intact", field_becomes(mixed), repr(edit_text(serial)))

    control.sendall(key_press(279))
    check("the clipboard got its content back", field_becomes(mixed + "ORIGINAL"), repr(edit_text(serial)))

    control.sendall(text_message("好的") + key_press(67))
    check("a key after text keeps its place", field_becomes(mixed + "ORIGINAL好"), repr(edit_text(serial)))

    beating.clear()
    video.close()
    with reader.lock:
        errors = list(reader.notices)
    check("no error notices", not errors, "; ".join(errors))
    return ok


def exercise_foreground(adb: list[str], sock: socket.socket, capabilities: int,
                        foreground: list[tuple[str, str, bool]]) -> bool:
    ok = True

    def check(name: str, passed: bool, detail: str = "") -> None:
        nonlocal ok
        ok = ok and passed
        print(f"  {'ok' if passed else 'FAILED'}: {name}{' (' + detail + ')' if detail else ''}")

    def latest() -> str:
        return foreground[-1][0] if foreground else "nothing"

    def wait_until(condition, timeout: float, since: int) -> bool:
        deadline = time.monotonic() + timeout
        last_ping = 0.0
        while not any(condition(package) for package, _, _ in foreground[since:]):
            now = time.monotonic()
            if now >= deadline:
                return False
            if now - last_ping >= 1:
                sock.sendall(frame(0x0401, struct.pack(">IQ", 7, time.monotonic_ns() // 1000)))
                last_ping = now
            readable, _, _ = select.select([sock], [], [], min(0.2, deadline - now))
            if readable:
                type_id, payload = read_frame(sock)
                if type_id == FOREGROUND_APP:
                    note_foreground(foreground, payload)
        return True

    def shown() -> str:
        lines = subprocess.run(adb + ["shell", "dumpsys activity activities | grep topResumedActivity"],
                               capture_output=True, text=True).stdout.strip().splitlines()
        return lines[0].strip() if lines else "unknown"

    def go_home() -> bool:
        since = len(foreground)
        if foreground and foreground[-1][0] == home:
            since -= 1
        for _ in range(3):
            subprocess.run(adb + ["shell", "input", "keyevent", "KEYCODE_HOME"], check=True, capture_output=True)
            if wait_until(lambda package: package == home, 4, since):
                return True
            print(f"  no report yet, the device shows {shown()}")
        return False

    check("capability 7 is set", bool(capabilities & CAPABILITY_FOREGROUND_APP), f"capabilities {capabilities:#x}")
    if not capabilities & CAPABILITY_FOREGROUND_APP:
        return False
    check("the foreground app is reported when control connects", len(foreground) >= 1, latest())
    resolved = subprocess.run(adb + ["shell", "cmd", "package", "resolve-activity", "--brief", "-a",
                                     "android.intent.action.MAIN", "-c", "android.intent.category.HOME"],
                              check=True, capture_output=True, text=True).stdout.strip().splitlines()
    home = resolved[-1].split("/")[0] if resolved else ""
    check("the home screen is found", home not in ("", "com.android.settings"), home or "nothing")
    check("going home is reported", go_home(), latest())
    since = len(foreground)
    subprocess.run(adb + ["shell", "am", "start", "-W", "-a", "android.settings.SETTINGS"], check=True,
                   capture_output=True)
    check("opening Settings is reported",
          wait_until(lambda package: package == "com.android.settings", 10, since), latest())
    marked = [game for package, _, game in foreground if package in ("com.android.settings", home)]
    check("Settings and the home screen are not reported as games", not any(marked),
          f"{sum(marked)} of {len(marked)} reports marked")
    check("going home again is reported", go_home(), latest())
    repeats = sum(1 for before, after in zip(foreground, foreground[1:]) if before == after)
    check("no report repeats the one before", repeats == 0, f"{repeats} repeats")
    return ok


def set_rotation(serial: str, quarter_turns: int) -> None:
    subprocess.run([find_adb(), "-s", serial, "shell", "settings put system accelerometer_rotation 0; "
                    f"settings put system user_rotation {quarter_turns}"], capture_output=True)


def keep_screen_busy(adb: list[str]) -> subprocess.Popen:
    subprocess.run(adb + ["shell", "input keyevent KEYCODE_WAKEUP; am start -W -a android.settings.SETTINGS"],
                   capture_output=True)
    return subprocess.Popen(adb + ["shell", "while true; do input swipe 540 1800 540 700 400; "
                                            "input swipe 540 700 540 1800 400; done"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def stream_video(port: int, token: bytes, control: socket.socket, args) -> bool:
    busy = keep_screen_busy([find_adb(), "-s", args.serial])
    try:
        return stream_video_while_busy(port, token, control, args)
    finally:
        busy.kill()
        subprocess.run([find_adb(), "-s", args.serial, "shell", "pkill -f 'input swip[e]'"], capture_output=True)


def stream_video_while_busy(port: int, token: bytes, control: socket.socket, args) -> bool:
    reader = ControlReader(control)
    reader.start()
    video = open_channel(port, token, 1, 64 * 1024 if args.throttle else 0)
    video.settimeout(10)
    control.sendall(configure_video(args.codec, args.max_size, args.max_fps, args.bitrate))
    pending_controls = [(0.3, "key frame request", frame(0x0204, b"")),
                        (0.5, "bitrate 1 Mbps", frame(0x0202, struct.pack(">I", 1_000_000))),
                        (0.75, "max fps 10", frame(0x0203, struct.pack(">H", 10)))] if args.controls else []
    phase_marks: list[tuple[int, str]] = []
    key_request_at = None
    key_after_request_ms = None
    frame_log: list[tuple[int, int]] = []
    stream_path = Path(__file__).resolve().parent.parent / "build" / "smoke" / f"{args.serial}.{args.codec}"
    stream_path.parent.mkdir(parents=True, exist_ok=True)
    sessions, frames, key_frames, discardable = [], 0, 0, 0
    first_frame_ms = None
    last_pts = None
    last_ping = 0.0
    latencies_ms: list[float] = []
    encode_ms: list[float] = []
    network_ms: list[float] = []
    rotated = False
    session_frames = 0
    session_counts: dict[int, int] = {}
    started = time.monotonic()
    record = Path(args.record).open("wb") if args.record else None
    throttle_bps = args.throttle * 1000
    read_until = started
    report_since = time.monotonic_ns() // 1000
    report_frames, report_bytes, report_delays = 0, 0, []
    second_start, second_bytes, second_network = started, 0, []

    def report_if_due() -> None:
        nonlocal report_since, report_frames, report_bytes, report_delays
        now_us = time.monotonic_ns() // 1000
        if sessions and now_us - report_since >= 200_000:
            with reader.lock:
                rtt = reader.best_rtt_us or 0
            delays = report_delays or [0]
            control.sendall(frame(0x0403, struct.pack(
                ">HIHIqqqIII", sessions[-1][0], now_us - report_since, min(report_frames, 0xFFFF),
                min(report_bytes, 0xFFFFFFFF), min(delays), sum(delays) // len(delays), max(delays),
                min(rtt, 0xFFFFFFFF), 0, 0)))
            report_since, report_frames, report_bytes, report_delays = now_us, 0, 0, []

    with stream_path.open("wb") as out:
        while time.monotonic() - started < args.video:
            if time.monotonic() - last_ping >= 1.0:
                control.sendall(frame(0x0401, struct.pack(">IQ", 7, time.monotonic_ns() // 1000)))
                last_ping = time.monotonic()
            if args.rotate and not rotated and time.monotonic() - started >= args.video / 2:
                set_rotation(args.serial, 1)
                rotated = True
            while pending_controls and time.monotonic() - started >= args.video * pending_controls[0][0]:
                _, name, message = pending_controls.pop(0)
                control.sendall(message)
                phase_marks.append((time.monotonic_ns() // 1000, name))
                if name == "key frame request":
                    key_request_at = time.monotonic()
            if throttle_bps:
                readable, _, _ = select.select([video], [], [], 0.05)
                if not readable:
                    report_if_due()
                    continue
            header = read_exactly(video, MEDIA_HEADER.size)
            kind, flags, session, pts, encode_delay, size = MEDIA_HEADER.unpack(header)
            payload = read_exactly(video, size)
            received_us = time.monotonic_ns() // 1000
            if record:
                record.write(header + payload)
            if throttle_bps:
                report_bytes += MEDIA_HEADER.size + size
                second_bytes += MEDIA_HEADER.size + size
                if kind == 2:
                    report_frames += 1
                    report_delays.append(received_us - pts - encode_delay)
            if kind == 0:
                info = VIDEO_SESSION.unpack(payload)
                sessions.append((session, info))
                print(f"  session {session}: {CODECS[info[0]]} {info[1]}x{info[2]} of display {info[3]} "
                      f"({info[4]}x{info[5]}, rotation {info[6]})")
                assert not sessions[:-1] or session != sessions[-2][0], "session number reused"
                session_frames = 0
                if len(sessions) > 1:
                    out.close()
                    out = Path(f"{stream_path}.{session}").open("wb")
            elif kind == 1:
                assert sessions, "codec configuration before the session description"
                if CODECS[sessions[-1][1][0]] == "av1":
                    print(f"  av1 configuration record: {len(payload)} bytes, first byte {payload[0]:#04x}")
                else:
                    out.write(payload)
            else:
                assert sessions, "frame before the session description"
                if session_frames == 0:
                    assert flags & 1, f"the first frame of session {session} is not a key frame"
                elif flags & 1 and key_request_at is not None and key_after_request_ms is None:
                    key_after_request_ms = (time.monotonic() - key_request_at) * 1000
                if frames == 0:
                    first_frame_ms = (time.monotonic() - started) * 1000
                frame_log.append((pts, size))
                assert last_pts is None or pts >= last_pts, f"ptsUs went backwards: {last_pts} -> {pts}"
                last_pts = pts
                frames += 1
                session_frames += 1
                key_frames += flags & 1
                discardable += (flags >> 1) & 1
                assert encode_delay > 0, f"frame {pts} carries no encode delay"
                encode_ms.append(encode_delay / 1000)
                with reader.lock:
                    offset = reader.offset_us
                if offset is not None:
                    latencies_ms.append((received_us + offset - pts) / 1000)
                    network_ms.append((received_us + offset - pts - encode_delay) / 1000)
                    second_network.append(network_ms[-1])
                session_counts[session] = session_frames
                out.write(payload)
            if throttle_bps:
                report_if_due()
                if time.monotonic() - second_start >= 1.0:
                    ordered = sorted(second_network)
                    rate = second_bytes * 8 / (time.monotonic() - second_start) / 1000
                    print(f"  {time.monotonic() - started:5.1f} s: {rate:5.0f} kbit/s read, network and send queue "
                          + (f"median {ordered[len(ordered) // 2]:.0f} ms" if ordered else "-"))
                    second_start, second_bytes, second_network = time.monotonic(), 0, []
                if not args.throttle_for or time.monotonic() - started < args.throttle_for:
                    read_until = max(read_until, time.monotonic()) + (MEDIA_HEADER.size + size) * 8 / throttle_bps
                    while (left := read_until - time.monotonic()) > 0:
                        time.sleep(min(left, 0.05))
                        report_if_due()
    out.close()
    if record:
        record.close()
    if rotated:
        set_rotation(args.serial, 0)
    elapsed = time.monotonic() - started
    with reader.lock:
        pongs = list(reader.pongs)
    print(f"  {frames} frames ({key_frames} key, {discardable} discardable) in {elapsed:.1f} s, "
          f"first frame {first_frame_ms:.0f} ms after ConfigureVideo; stream written to {stream_path}")
    if encode_ms:
        ordered = sorted(encode_ms)
        print(f"  capture to encoder output: median {ordered[len(ordered) // 2]:.1f} ms, "
              f"p95 {ordered[int(len(ordered) * 0.95)]:.1f} ms")
    if latencies_ms:
        ordered = sorted(latencies_ms)
        network = sorted(network_ms)
        print(f"  capture to receipt: median {ordered[len(ordered) // 2]:.1f} ms, "
              f"p95 {ordered[int(len(ordered) * 0.95)]:.1f} ms; of which network and send queue: median "
              f"{network[len(network) // 2]:.1f} ms (clock offset from the best of "
              f"{len(pongs)} pings, round trip {reader.best_rtt_us / 1000:.1f} ms)")
    if throttle_bps and len(network_ms) >= 3:
        third = len(network_ms) // 3
        middle = sorted(network_ms[third:2 * third])
        last = sorted(network_ms[2 * third:])
        middle_ms, last_ms = middle[len(middle) // 2], last[len(last) // 2]
        print(f"  throttled to {args.throttle} kbit/s: network and send queue median {middle_ms:.0f} ms in the "
              f"middle third, {last_ms:.0f} ms in the last third")
        assert last_ms < 300, "the delay kept growing under the throttle"
    if args.controls and frame_log and reader.offset_us is not None:
        offset = reader.offset_us
        bounds = [frame_log[0][0]] + [mark + offset for mark, _ in phase_marks] + [frame_log[-1][0] + 1]
        names = ["initial"] + [name for _, name in phase_marks]
        for (begin, end), name in zip(zip(bounds, bounds[1:]), names):
            window = [n for pts, n in frame_log if begin + 500_000 <= pts < end]
            span = max((end - begin - 500_000) / 1e6, 0.001)
            print(f"  phase {name!r}: {len(window) / span:.1f} fps, {sum(window) * 8 / span / 1000:.0f} kbit/s "
                  f"over {span:.1f} s")
        print(f"  key frame {key_after_request_ms:.0f} ms after the request" if key_after_request_ms is not None
              else "  no key frame after the request")
        assert key_after_request_ms is not None, "RequestKeyFrame produced no key frame"
    if args.rotate:
        assert len(sessions) >= 2, "rotation did not start a new session"
        before, after = sessions[0][1], sessions[-1][1]
        assert (after[4], after[5]) == (before[5], before[4]) and after[6] == 1 and after[1] > after[2], \
            f"unexpected rotated session {after}"
    if pongs and last_pts is not None:
        lag_ms = (pongs[-1][1] - last_pts) / 1000
        print(f"  newest Pong.serverTimeUs {pongs[-1][1]} - newest ptsUs {last_pts} = {lag_ms:.0f} ms")
        assert -2000 < lag_ms < 5000, "ptsUs and Pong.serverTimeUs do not share a clock"
    video.close()
    ok = frames > 0 and not reader.notices
    if args.ffprobe:
        for index, (session, info) in enumerate(sessions):
            path = stream_path if index == 0 else Path(f"{stream_path}.{session}")
            demuxer = ["-f", "obu"] if CODECS[info[0]] == "av1" else []
            probe = subprocess.run([args.ffprobe, "-v", "error", *demuxer, "-count_frames", "-select_streams", "v:0",
                                    "-show_entries", "stream=codec_name,width,height,nb_read_frames", "-of",
                                    "default=noprint_wrappers=1", str(path)], capture_output=True, text=True)
            decoded = dict(line.split("=", 1) for line in probe.stdout.split() if "=" in line)
            expected = session_counts.get(session, 0)
            print(f"  ffprobe {path.name}: " + " ".join(probe.stdout.split())
                  + f" (received {expected})" + (f" | {probe.stderr.strip()}" if probe.stderr else ""))
            ok = ok and probe.returncode == 0 and int(decoded.get("nb_read_frames", 0)) == expected
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", default="emulator-5556")
    parser.add_argument("--apk", default="android/server/build/outputs/apk/release/server-release-unsigned.apk")
    parser.add_argument("--abandon", action="store_true",
                        help="drop the connection without Goodbye and check that the server cleans up (SRV-008)")
    parser.add_argument("--video", type=float, default=0, help="stream video for this many seconds")
    parser.add_argument("--codec", choices=sorted(CODEC_IDS), default="h264")
    parser.add_argument("--max-size", type=int, default=0)
    parser.add_argument("--bitrate", type=int, default=8_000_000, help="video bitrate in bit/s")
    parser.add_argument("--audio", type=float, default=0,
                        help="capture the device's audio output for this many seconds (Android 11+)")
    parser.add_argument("--record", help="also write every video packet as received, header included (SPEC 3.2)")
    parser.add_argument("--ffprobe", help="decode the recorded stream with this ffprobe")
    parser.add_argument("--log", choices=["trace", "debug", "info", "warning", "error"], default="info")
    parser.add_argument("--rotate", action="store_true", help="rotate to landscape halfway (SRV-005)")
    parser.add_argument("--controls", action="store_true",
                        help="request a key frame, lower the bitrate and cap the frame rate while streaming (SRV-004)")
    parser.add_argument("--max-fps", type=int, default=0)
    parser.add_argument("--throttle", type=int, default=0,
                        help="read video at most this many kbit/s, a narrow link, and send receiver reports as the "
                             "client does (MED-012)")
    parser.add_argument("--throttle-for", type=float, default=0,
                        help="lift the throttle after this many seconds, a link that widens again, while reports "
                             "go on (ADR-0054: the fallback steps back up)")
    parser.add_argument("--input", action="store_true",
                        help="inject touch, multi-touch, wheel and keys and check their effect (SRV-006, PRO-007); "
                             "ends by dropping the connection")
    parser.add_argument("--text", action="store_true",
                        help="type mixed text into a search field and check clipboard restoration (SRV-007)")
    parser.add_argument("--foreground", action="store_true",
                        help="open Settings, go home and check the foreground app reports (capability 7)")
    args = parser.parse_args()
    if args.input:
        args.abandon = True
    linger_ms = 1000 if args.abandon else 0
    adb = [find_adb(), "-s", args.serial]

    token = secrets.token_bytes(16)
    socket_name = "gamescrcpy_" + secrets.token_hex(8)
    subprocess.run(adb + ["push", args.apk, REMOTE_JAR], check=True, capture_output=True)

    started = time.monotonic()
    server = subprocess.Popen(
        adb + ["shell", f"CLASSPATH={REMOTE_JAR} app_process / com.gamescrcpy.server.Main "
                        f"{PROTOCOL_VERSION} socket={socket_name} lingerMs={linger_ms} log={args.log}"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert server.stdin and server.stdout and server.stderr
    server_log: list[str] = []

    def drain_server_log() -> None:
        for line in server.stderr:
            server_log.append(f"{time.monotonic() - started:6.1f} s  {line.rstrip()}")

    log_reader = threading.Thread(target=drain_server_log, daemon=True)
    log_reader.start()
    server.stdin.write(f"GS_TOKEN={token.hex()}\n")
    server.stdin.flush()
    ready = server.stdout.readline().strip()
    ready_ms = (time.monotonic() - started) * 1000
    print(f"server says: {ready!r} after {ready_ms:.0f} ms")
    if ready != f"GS_READY {PROTOCOL_VERSION}":
        server.kill()
        log_reader.join(2)
        print("\n".join(server_log), file=sys.stderr)
        return 1

    port = int(subprocess.run(adb + ["forward", "tcp:0", f"localabstract:{socket_name}"],
                              check=True, capture_output=True, text=True).stdout.strip())
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            type_id, payload = read_frame(sock)
            magic, version = struct.unpack(">IH", payload[:6])
            nonce = payload[6:22]
            assert type_id == 0x8001 and magic == MAGIC and version == PROTOCOL_VERSION, (hex(type_id), hex(magic))

            proof = hmac.new(token, b"GSCP/1 hello" + nonce, hashlib.sha256).digest()
            client_version = b"smoke"
            sock.sendall(frame(0x0002, struct.pack(">H", PROTOCOL_VERSION) + bytes([len(client_version)])
                               + client_version + proof))
            type_id, payload = read_frame(sock)
            assert type_id == 0x8003, f"expected Welcome, got {type_id:#06x}: {payload!r}"
            fields = parse_welcome(payload)
            handshake_ms = (time.monotonic() - started) * 1000
            print(f"handshake done {handshake_ms:.0f} ms after launch")
            for key, value in fields.items():
                print(f"  {key}: {value}")
            assert fields["displays"], "Welcome lists no display"
            assert any(e.startswith("h264:") for e in fields["encoders"]), "Welcome lists no H.264 encoder"

            foreground: list[tuple[str, str, bool]] = []
            sock.sendall(frame(0x0401, struct.pack(">IQ", 42, 1234)))
            payload = read_reply(sock, 0x8401, foreground)
            nonce_back, client_time, server_time = struct.unpack(">IQQ", payload)
            assert nonce_back == 42 and client_time == 1234 and server_time > 0
            print(f"pong: serverTimeUs={server_time}")

            video_ok = True
            if args.foreground:
                video_ok = exercise_foreground(adb, sock, fields["capabilities"], foreground)
                print(f"foreground: {'ok' if video_ok else 'FAILED'}")
            if args.video > 0:
                video_ok = stream_video(port, token, sock, args)
                print(f"video: {'ok' if video_ok else 'FAILED'}")
            if args.input:
                video_ok = exercise_input(port, token, sock, args)
                print(f"input: {'ok' if video_ok else 'FAILED'}")
            if args.text:
                video_ok = exercise_text(port, token, sock, args) and video_ok
                print(f"text: {'ok' if video_ok else 'FAILED'}")
            if args.audio > 0:
                video_ok = stream_audio(port, token, sock, args.audio) and video_ok
                print(f"audio: {'ok' if video_ok else 'FAILED'}")

            if not args.abandon:
                sock.sendall(frame(0x0402, struct.pack(">H", 0)))
            if not video_ok:
                return 1
        if args.abandon:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                alive = subprocess.run(adb + ["shell", "pgrep", "-f", "com.gamescrcpy.server.Main"],
                                       capture_output=True, text=True).stdout.strip()
                if not alive:
                    print(f"server process gone {(time.monotonic() - started) * 1000:.0f} ms after launch")
                    break
                time.sleep(0.2)
            else:
                print("server process still running after the client vanished", file=sys.stderr)
                return 1
        server.wait(timeout=10)
        log_reader.join(2)
        print(f"server exited with {server.returncode}; stderr, seconds after launch:\n" + "\n".join(server_log))
        return 0 if server.returncode == 0 else 1
    except Exception:
        if server.poll() is None:
            server.kill()
        log_reader.join(2)
        print("server stderr, seconds after launch:\n" + "\n".join(server_log), file=sys.stderr)
        raise
    finally:
        subprocess.run(adb + ["forward", "--remove", f"tcp:{port}"], capture_output=True)
        subprocess.run(adb + ["shell", "rm", "-f", REMOTE_JAR], capture_output=True)
        if server.poll() is None:
            server.kill()


if __name__ == "__main__":
    sys.exit(main())
