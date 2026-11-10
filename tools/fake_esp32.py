from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import socket
import struct
import sys
import time
from pathlib import Path

MAGIC = bytes([0xA5, 0x5A])
OVERHEAD = 8
TAG_SIZE = 16
MAX_PAYLOAD = 512

HELLO_REQ = 0x01
TOUCH = 0x02
KEYBOARD = 0x03
CONSUMER = 0x04
MOUSE = 0x05
GAMEPAD = 0x06
RELEASE_ALL = 0x07
PING = 0x08
SET_BAUD = 0x09
WIFI_SET = 0x0A
WIFI_FORGET = 0x0B
SET_NAME = 0x0C
REBOOT = 0x0D
ENTER_DOWNLOAD = 0x0E
HELLO = 0x81
STATUS = 0x82
PONG = 0x83
LOG = 0x84
ERROR = 0x85

FEATURE_USB_HID = 0x01
FEATURE_BLE_HID = 0x04
FEATURE_WIFI = 0x08
ERROR_MALFORMED = 1
ERROR_UNKNOWN_TYPE = 2
ERROR_UNSUPPORTED = 3
SAFETY_SECONDS = 2.0

CHIPS = {
    "s3": (9, "ESP32-S3", FEATURE_USB_HID | FEATURE_WIFI, "usb"),
    "s2": (2, "ESP32-S2", FEATURE_USB_HID | FEATURE_WIFI, "usb"),
    "c3": (5, "ESP32-C3", FEATURE_BLE_HID | FEATURE_WIFI, "ble"),
    "c6": (13, "ESP32-C6", FEATURE_BLE_HID | FEATURE_WIFI, "ble"),
    "esp32": (0, "ESP32", FEATURE_BLE_HID | FEATURE_WIFI, "ble"),
}

DEFAULT_KEY = hashlib.sha256(b"gamescrcpy fake esp32 board").hexdigest()
DEFAULT_MAC = "fa4e00e5032a"


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def text(value: str, most: int) -> bytes:
    raw = value.encode("utf-8")[:most]
    return bytes([len(raw)]) + raw


def read_text(payload: bytes, at: int) -> tuple[str, int]:
    size = payload[at]
    raw = payload[at + 1:at + 1 + size]
    if len(raw) != size:
        raise ValueError("truncated text")
    return raw.decode("utf-8", errors="replace"), at + 1 + size


class Board:
    def __init__(self, args: argparse.Namespace) -> None:
        self.key = bytes.fromhex(args.key)
        self.chip_id, chip_name, self.features, self.phone_kind = CHIPS[args.chip]
        self.name = args.name or f"{chip_name}（模拟）"
        self.mac = bytes.fromhex(args.mac)
        self.firmware = args.firmware
        self.phone = args.phone if args.phone != "auto" else self.phone_kind
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind((args.host, args.port))
        self.socket.settimeout(0.1)
        self.peer: tuple[str, int] | None = None
        self.seq = 0
        self.last_frame = time.monotonic()
        self.holding = False
        self.pushed_status: bytes | None = None
        self.started = time.monotonic()
        self.phone_delay = args.phone_delay

    def say(self, line: str) -> None:
        stamp = time.strftime("%H:%M:%S")
        print(f"[{stamp}] {line}", flush=True)

    def frame(self, kind: int, payload: bytes) -> bytes:
        body = bytes([kind, self.seq & 0xFF]) + struct.pack("<H", len(payload)) + payload
        self.seq += 1
        return MAGIC + body + struct.pack("<H", crc16(body))

    def send(self, kind: int, payload: bytes, to: tuple[str, int] | None = None) -> None:
        target = to or self.peer
        if target is None:
            return
        data = self.frame(kind, payload)
        tag = hmac.new(self.key, data, hashlib.sha256).digest()[:TAG_SIZE]
        self.socket.sendto(data + tag, target)

    def ipv4(self) -> bytes:
        if self.peer is None:
            return bytes(4)
        try:
            return socket.inet_aton(self.peer[0])
        except OSError:
            return bytes(4)

    def status_payload(self) -> bytes:
        ready = time.monotonic() - self.started >= self.phone_delay
        usb = 0
        ble = 0
        peer = ""
        if self.phone == "usb":
            usb = 2 if ready else 1
        elif self.phone == "ble":
            ble = 3 if ready else 2
            peer = "Pixel 8（模拟）" if ready else ""
        return bytes([usb, ble]) + text(peer, 32) + bytes([3]) + self.ipv4()

    def hello_payload(self) -> bytes:
        return (bytes([2]) + text(self.firmware, 16) + struct.pack("<HHI", self.chip_id, 0, self.features) + self.mac
                + bytes([3]) + self.ipv4() + text(self.name, 32))

    def error(self, code: int, message: str, to: tuple[str, int]) -> None:
        self.send(ERROR, struct.pack("<H", code) + text(message, 255), to)

    def release(self, reason: str) -> None:
        if self.holding:
            self.say(f"RELEASE everything ({reason})")
        self.holding = False

    def describe_touch(self, payload: bytes) -> str:
        count = payload[0]
        if count > 10 or len(payload) < 1 + 6 * count:
            raise ValueError("bad touch")
        parts = []
        down = False
        for index in range(count):
            contact, tip, x, y = struct.unpack_from("<BBHH", payload, 1 + 6 * index)
            down = down or tip == 1
            parts.append(f"#{contact} {'down' if tip else 'up'} ({x}, {y})")
        self.holding = down
        return f"TOUCH {count} contact(s): " + ", ".join(parts) if parts else "TOUCH none"

    def handle(self, kind: int, payload: bytes, sender: tuple[str, int]) -> None:
        self.peer = sender
        self.last_frame = time.monotonic()
        if kind == HELLO_REQ:
            self.say(f"HELLO_REQ from {sender[0]}:{sender[1]}")
            self.send(HELLO, self.hello_payload(), sender)
            self.pushed_status = None
        elif kind == PING:
            (token,) = struct.unpack_from("<I", payload)
            self.send(PONG, struct.pack("<I", token), sender)
        elif kind == SET_BAUD:
            baud, token = struct.unpack_from("<II", payload)
            self.say(f"SET_BAUD {baud} (ignored over Wi-Fi)")
            self.send(PONG, struct.pack("<I", token), sender)
        elif kind == TOUCH:
            self.say(self.describe_touch(payload))
        elif kind == KEYBOARD:
            modifiers = payload[0]
            keys = [f"0x{key:02X}" for key in payload[1:7] if key]
            self.holding = modifiers != 0 or bool(keys)
            self.say(f"KEYBOARD modifiers 0x{modifiers:02X} keys {' '.join(keys) or '-'}")
        elif kind == CONSUMER:
            (usage,) = struct.unpack_from("<H", payload)
            self.say(f"CONSUMER 0x{usage:04X}")
        elif kind == MOUSE:
            buttons, dx, dy, wheel = struct.unpack_from("<Bhhb", payload)
            self.say(f"MOUSE buttons 0x{buttons:02X} move ({dx}, {dy}) wheel {wheel}")
        elif kind == GAMEPAD:
            buttons, hat, lx, ly, rx, ry, lt, rt = struct.unpack_from("<IBhhhhBB", payload)
            self.say(f"GAMEPAD buttons 0x{buttons:08X} hat {hat} left ({lx}, {ly}) right ({rx}, {ry}) "
                     f"triggers {lt} {rt}")
        elif kind == RELEASE_ALL:
            self.holding = True
            self.release("RELEASE_ALL")
        elif kind == SET_NAME:
            self.name, _ = read_text(payload, 0)
            self.say(f"SET_NAME {self.name}")
            self.send(HELLO, self.hello_payload(), sender)
        elif kind in (WIFI_SET, WIFI_FORGET):
            self.error(ERROR_UNSUPPORTED, "Wi-Fi is set over the serial port", sender)
        elif kind in (REBOOT, ENTER_DOWNLOAD):
            self.error(ERROR_UNSUPPORTED, "the fake board cannot restart", sender)
        elif kind < 0x80:
            self.error(ERROR_UNKNOWN_TYPE, "unknown message", sender)

    def receive(self) -> None:
        try:
            datagram, sender = self.socket.recvfrom(2048)
        except socket.timeout:
            return
        except ConnectionResetError:
            return
        if len(datagram) < OVERHEAD + TAG_SIZE:
            return
        data, tag = datagram[:-TAG_SIZE], datagram[-TAG_SIZE:]
        if not hmac.compare_digest(hmac.new(self.key, data, hashlib.sha256).digest()[:TAG_SIZE], tag):
            self.say(f"dropped a datagram from {sender[0]}:{sender[1]} with a wrong tag")
            return
        if data[:2] != MAGIC:
            return
        kind, _, length = struct.unpack_from("<BBH", data, 2)
        if length > MAX_PAYLOAD or len(data) != OVERHEAD + length:
            return
        if crc16(data[2:6 + length]) != struct.unpack_from("<H", data, 6 + length)[0]:
            return
        try:
            self.handle(kind, data[6:6 + length], sender)
        except (ValueError, struct.error, IndexError):
            self.error(ERROR_MALFORMED, "malformed message", sender)

    def tick(self) -> None:
        if self.holding and time.monotonic() - self.last_frame > SAFETY_SECONDS:
            self.release("nothing from the computer for 2 s")
            self.send(LOG, bytes([1]) + text("nothing from the computer for 2 s: released every touch", 255))
        status = self.status_payload()
        if self.peer is not None and status != self.pushed_status:
            self.pushed_status = status
            self.send(STATUS, status)

    def run(self) -> None:
        host, port = self.socket.getsockname()
        self.say(f"fake {self.name} listening on UDP {host}:{port}, board id {self.mac.hex()}")
        while True:
            self.receive()
            self.tick()


def register(folder: Path, args: argparse.Namespace) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / "esp32-boards.json"
    document = {"format": "gamescrcpy.esp32-boards", "version": 1, "boards": {}}
    if path.exists():
        try:
            loaded = json.loads(path.read_text(encoding="utf-8"))
            if loaded.get("format") == document["format"] and isinstance(loaded.get("boards"), dict):
                document["boards"] = loaded["boards"]
        except (OSError, ValueError):
            pass
    chip_name = CHIPS[args.chip][1]
    document["boards"][args.mac] = {
        "name": args.name or f"{chip_name}（模拟）",
        "key": args.key,
        "address": args.host,
    }
    path.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"registered board {args.mac} in {path}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="A fake GameScrcpy ESP32 touch box on UDP, for walkthroughs.")
    parser.add_argument("--host", default="127.0.0.1", help="address to listen on (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=4545, help="UDP port (default 4545)")
    parser.add_argument("--chip", choices=sorted(CHIPS), default="s3", help="chip to pretend to be (default s3)")
    parser.add_argument("--name", default="", help="board name (default '<chip>（模拟）')")
    parser.add_argument("--firmware", default="1.0.0", help="firmware version to report")
    parser.add_argument("--mac", default=DEFAULT_MAC, help="12 lowercase hex digits, the board id")
    parser.add_argument("--key", default=DEFAULT_KEY, help="64 hex digits, the Wi-Fi key")
    parser.add_argument("--phone", choices=["auto", "usb", "ble", "none"], default="auto",
                        help="what the phone side reports (default: USB for S2/S3, Bluetooth otherwise)")
    parser.add_argument("--phone-delay", type=float, default=3.0,
                        help="seconds until the phone side reports connected (default 3)")
    parser.add_argument("--register", type=Path, default=None,
                        help="data folder (GS_DATA_DIR) to write the board and its key into esp32-boards.json")
    args = parser.parse_args()
    if len(bytes.fromhex(args.key)) != 32:
        parser.error("--key must be 64 hex digits")
    if len(args.mac) != 12 or any(c not in "0123456789abcdef" for c in args.mac):
        parser.error("--mac must be 12 lowercase hex digits")
    if args.register is not None:
        register(args.register, args)
    try:
        Board(args).run()
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
