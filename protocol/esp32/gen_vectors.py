from __future__ import annotations

import hashlib
import hmac
import json
import struct
from pathlib import Path

MAGIC0 = 0xA5
MAGIC1 = 0x5A
HEADER = 6
OVERHEAD = 8
MAX_PAYLOAD = 512
MAX_FRAME = 520
TAG_SIZE = 16

TYPES = {
    "hello_req": 0x01,
    "touch": 0x02,
    "keyboard": 0x03,
    "consumer": 0x04,
    "mouse": 0x05,
    "gamepad": 0x06,
    "release_all": 0x07,
    "ping": 0x08,
    "set_baud": 0x09,
    "wifi_set": 0x0A,
    "wifi_forget": 0x0B,
    "set_name": 0x0C,
    "reboot": 0x0D,
    "enter_download": 0x0E,
    "hello": 0x81,
    "status": 0x82,
    "pong": 0x83,
    "log": 0x84,
    "error": 0x85,
}


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def frame(kind: int, seq: int, payload: bytes) -> bytes:
    assert len(payload) <= MAX_PAYLOAD
    body = bytes([kind, seq]) + struct.pack("<H", len(payload)) + payload
    return bytes([MAGIC0, MAGIC1]) + body + struct.pack("<H", crc16(body))


def text(value: str, most: int) -> bytes:
    raw = value.encode("utf-8")
    assert len(raw) <= most
    return bytes([len(raw)]) + raw


def encode_hello_req(fields: dict) -> bytes:
    return bytes([fields["version"]])


def encode_touch(fields: dict) -> bytes:
    contacts = fields["contacts"]
    out = bytes([len(contacts)])
    for contact in contacts:
        out += struct.pack("<BBHH", contact["id"], contact["tip"], contact["x"], contact["y"])
    return out


def encode_keyboard(fields: dict) -> bytes:
    assert len(fields["keys"]) == 6
    return bytes([fields["modifiers"]]) + bytes(fields["keys"])


def encode_consumer(fields: dict) -> bytes:
    return struct.pack("<H", fields["usage"])


def encode_mouse(fields: dict) -> bytes:
    return struct.pack("<Bhhb", fields["buttons"], fields["dx"], fields["dy"], fields["wheel"])


def encode_gamepad(fields: dict) -> bytes:
    return struct.pack("<IBhhhhBB", fields["buttons"], fields["hat"], fields["lx"], fields["ly"], fields["rx"],
                       fields["ry"], fields["lt"], fields["rt"])


def encode_token(fields: dict) -> bytes:
    return struct.pack("<I", fields["token"])


def encode_set_baud(fields: dict) -> bytes:
    return struct.pack("<II", fields["baud"], fields["token"])


def encode_wifi_set(fields: dict) -> bytes:
    key = bytes.fromhex(fields["key"])
    assert len(key) == 32
    return text(fields["ssid"], 32) + text(fields["password"], 64) + key


def encode_name(fields: dict) -> bytes:
    return text(fields["name"], 32)


def encode_hello(fields: dict) -> bytes:
    mac = bytes.fromhex(fields["mac"])
    assert len(mac) == 6
    return (bytes([fields["protocol"]]) + text(fields["firmware"], 16)
            + struct.pack("<HHI", fields["chipId"], fields["chipRevision"], fields["features"]) + mac
            + bytes([fields["wifi"]]) + bytes(fields["ipv4"]) + text(fields["name"], 32))


def encode_status(fields: dict) -> bytes:
    return (bytes([fields["usb"], fields["ble"]]) + text(fields["peer"], 32) + bytes([fields["wifi"]])
            + bytes(fields["ipv4"]))


def encode_log(fields: dict) -> bytes:
    return bytes([fields["level"]]) + text(fields["text"], 255)


def encode_error(fields: dict) -> bytes:
    return struct.pack("<H", fields["code"]) + text(fields["text"], 255)


def encode_empty(fields: dict) -> bytes:
    assert not fields
    return b""


ENCODERS = {
    "hello_req": encode_hello_req,
    "touch": encode_touch,
    "keyboard": encode_keyboard,
    "consumer": encode_consumer,
    "mouse": encode_mouse,
    "gamepad": encode_gamepad,
    "release_all": encode_empty,
    "ping": encode_token,
    "set_baud": encode_set_baud,
    "wifi_set": encode_wifi_set,
    "wifi_forget": encode_empty,
    "set_name": encode_name,
    "reboot": encode_empty,
    "enter_download": encode_empty,
    "hello": encode_hello,
    "status": encode_status,
    "pong": encode_token,
    "log": encode_log,
    "error": encode_error,
}

KEY = bytes(range(0x20, 0x40))

MESSAGES = [
    ("hello-request", "hello_req", 0, {"version": 2}),
    ("touch-none", "touch", 1, {"contacts": []}),
    ("touch-one-down", "touch", 2, {"contacts": [{"id": 0, "tip": 1, "x": 16384, "y": 16384}]}),
    ("touch-two-one-lifting", "touch", 3, {"contacts": [
        {"id": 0, "tip": 0, "x": 120, "y": 32767},
        {"id": 1, "tip": 1, "x": 32767, "y": 0},
    ]}),
    ("touch-ten", "touch", 4, {"contacts": [
        {"id": i, "tip": 1, "x": 3000 * i + 7, "y": 32767 - 3000 * i} for i in range(10)
    ]}),
    ("touch-high-id", "touch", 255, {"contacts": [{"id": 127, "tip": 1, "x": 0x5AA5, "y": 0x7FFF}]}),
    ("keyboard-shift-a", "keyboard", 5, {"modifiers": 0x02, "keys": [0x04, 0, 0, 0, 0, 0]}),
    ("keyboard-six", "keyboard", 6, {"modifiers": 0x05, "keys": [0x04, 0x05, 0x06, 0x07, 0x28, 0xE9]}),
    ("consumer-home", "consumer", 7, {"usage": 0x0223}),
    ("consumer-release", "consumer", 8, {"usage": 0}),
    ("mouse-move", "mouse", 9, {"buttons": 1, "dx": -5, "dy": 300, "wheel": -1}),
    ("mouse-extremes", "mouse", 10, {"buttons": 0x1F, "dx": -32767, "dy": 32767, "wheel": 127}),
    ("gamepad-centred", "gamepad", 11, {"buttons": 0, "hat": 8, "lx": 0, "ly": 0, "rx": 0, "ry": 0, "lt": 0, "rt": 0}),
    ("gamepad-full", "gamepad", 12, {"buttons": 0x80000001, "hat": 3, "lx": -32767, "ly": 32767, "rx": 1234,
                                     "ry": -4321, "lt": 255, "rt": 17}),
    ("release-all", "release_all", 13, {}),
    ("ping", "ping", 14, {"token": 0x12345678}),
    ("set-baud", "set_baud", 15, {"baud": 2000000, "token": 5}),
    ("wifi-set", "wifi_set", 16, {"ssid": "HomeNet", "password": "secret pass", "key": KEY.hex()}),
    ("wifi-set-open-network", "wifi_set", 17, {"ssid": "咖啡店", "password": "", "key": bytes(32).hex()}),
    ("wifi-forget", "wifi_forget", 18, {}),
    ("set-name", "set_name", 19, {"name": "Desk box"}),
    ("reboot", "reboot", 20, {}),
    ("enter-download", "enter_download", 21, {}),
    ("hello-s3", "hello", 0, {"protocol": 2, "firmware": "1.0.0", "chipId": 9, "chipRevision": 2,
                              "features": 0x09, "mac": "a1b2c3d4e5f6", "wifi": 3, "ipv4": [192, 168, 1, 20],
                              "name": "GameScrcpy S3"}),
    ("hello-p4", "hello", 1, {"protocol": 2, "firmware": "1.0.0-rc.1", "chipId": 18, "chipRevision": 100,
                              "features": 0x13, "mac": "001122334455", "wifi": 0, "ipv4": [0, 0, 0, 0],
                              "name": "P4"}),
    ("hello-c3", "hello", 2, {"protocol": 2, "firmware": "1.2.3", "chipId": 5, "chipRevision": 4,
                              "features": 0x1C, "mac": "deadbeef0102", "wifi": 1, "ipv4": [0, 0, 0, 0],
                              "name": "C3 盒子"}),
    ("status-usb-ready", "status", 3, {"usb": 2, "ble": 0, "peer": "", "wifi": 1, "ipv4": [0, 0, 0, 0]}),
    ("status-ble-connected", "status", 4, {"usb": 0, "ble": 3, "peer": "Pixel 8", "wifi": 3,
                                           "ipv4": [10, 0, 0, 7]}),
    ("pong", "pong", 5, {"token": 0xFFFFFFFF}),
    ("log", "log", 6, {"level": 2, "text": "boot ok"}),
    ("error", "error", 7, {"code": 3, "text": "unsupported"}),
]

INVALID = [
    ("hello-request-empty", "hello_req", ""),
    ("touch-eleven", "touch", "0b" + "000100000000" * 11),
    ("touch-truncated", "touch", "02" + "000110001000" + "0101"),
    ("touch-tip-two", "touch", "01" + "000200100010"),
    ("touch-x-too-big", "touch", "01" + "000100800010"),
    ("touch-id-too-big", "touch", "01" + "800100100010"),
    ("keyboard-short", "keyboard", "020400000000"),
    ("consumer-short", "consumer", "23"),
    ("mouse-short", "mouse", "01fbff2c01"),
    ("gamepad-hat-nine", "gamepad", "00000000" + "09" + "0000" * 4 + "0000"),
    ("gamepad-short", "gamepad", "00000000" + "08" + "0000" * 4 + "00"),
    ("ping-short", "ping", "785634"),
    ("set-baud-short", "set_baud", "80841e00"),
    ("wifi-set-empty-ssid", "wifi_set", "00" + "00" + "00" * 32),
    ("wifi-set-ssid-too-long", "wifi_set", "21" + "41" * 33 + "00" + "00" * 32),
    ("wifi-set-password-too-long", "wifi_set", "0141" + "41" + "42" * 65 + "00" * 32),
    ("wifi-set-key-short", "wifi_set", "0141" + "00" + "00" * 31),
    ("wifi-set-nul-in-ssid", "wifi_set", "024100" + "00" + "00" * 32),
    ("set-name-empty", "set_name", "00"),
    ("set-name-too-long", "set_name", "21" + "61" * 33),
    ("set-name-truncated", "set_name", "05616263"),
    ("hello-firmware-too-long", "hello", "02" + "11" + "31" * 17 + "0900" + "0200" + "09000000" + "00" * 6
     + "01" + "00" * 4 + "00"),
    ("hello-truncated", "hello", "0205312e302e3009000200"),
    ("status-truncated", "status", "0200"),
    ("pong-short", "pong", ""),
    ("log-truncated", "log", "020568656c6c"),
    ("error-short", "error", "03"),
]


def bad_crc(data: bytes) -> bytes:
    corrupted = bytearray(data)
    corrupted[HEADER] ^= 0x01
    return bytes(corrupted)


def frame_vectors() -> list[dict]:
    ping = frame(TYPES["ping"], 9, struct.pack("<I", 0x0BADF00D))
    empty = frame(TYPES["release_all"], 1, b"")
    oversized = bytes([MAGIC0, MAGIC1, TYPES["log"], 0]) + struct.pack("<H", MAX_PAYLOAD + 1)
    oversized += bytes(MAX_PAYLOAD + 1)
    oversized += struct.pack("<H", crc16(oversized[2:]))
    return [
        {"name": "truncated-crc", "bytes": ping[:-1].hex()},
        {"name": "truncated-header", "bytes": ping[:5].hex()},
        {"name": "truncated-payload", "bytes": (ping[:7] + ping[-2:]).hex()},
        {"name": "bad-crc", "bytes": bad_crc(ping).hex()},
        {"name": "bad-crc-empty", "bytes": (empty[:-1] + bytes([empty[-1] ^ 0x80])).hex()},
        {"name": "wrong-magic", "bytes": (bytes([0xA5, 0x5B]) + ping[2:]).hex()},
        {"name": "length-too-large", "bytes": oversized.hex()},
        {"name": "trailing-byte", "bytes": (ping + b"\x00").hex()},
    ]


class Parser:
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.ready = 0
        self.dropped = 0
        self.bad = 0

    def consume(self) -> None:
        if self.ready:
            del self.buffer[:self.ready]
            self.ready = 0

    def feed(self, data: bytes) -> int:
        self.consume()
        take = min(MAX_FRAME - len(self.buffer), len(data))
        self.buffer += data[:take]
        return take

    def skip(self) -> None:
        nxt = 1
        while nxt < len(self.buffer) and self.buffer[nxt] != MAGIC0:
            nxt += 1
        self.dropped += nxt
        del self.buffer[:nxt]

    def next(self) -> bytes | None:
        self.consume()
        while True:
            if not self.buffer:
                return None
            if self.buffer[0] != MAGIC0:
                self.skip()
                continue
            if len(self.buffer) < 2:
                return None
            if self.buffer[1] != MAGIC1:
                self.skip()
                continue
            if len(self.buffer) < HEADER:
                return None
            length = self.buffer[4] | (self.buffer[5] << 8)
            if length > MAX_PAYLOAD:
                self.bad += 1
                self.skip()
                continue
            total = OVERHEAD + length
            if len(self.buffer) < total:
                return None
            stored = self.buffer[HEADER + length] | (self.buffer[HEADER + length + 1] << 8)
            if crc16(bytes(self.buffer[2:HEADER + length])) != stored:
                self.bad += 1
                self.skip()
                continue
            self.ready = total
            return bytes(self.buffer[:total])


def run_parser(data: bytes, chunk: int) -> tuple[list[bytes], int, int]:
    parser = Parser()
    frames: list[bytes] = []
    at = 0
    while at < len(data):
        taken = parser.feed(data[at:at + chunk])
        at += taken
        while (found := parser.next()) is not None:
            frames.append(found)
    return frames, parser.bad, parser.dropped


def stream_vectors() -> list[dict]:
    a = frame(TYPES["pong"], 1, struct.pack("<I", 1))
    b = frame(TYPES["log"], 2, text("A5 5A inside", 255))
    c = frame(TYPES["status"], 3, encode_status({"usb": 2, "ble": 0, "peer": "", "wifi": 1, "ipv4": [0, 0, 0, 0]}))
    tricky = frame(TYPES["log"], 4, bytes([1]) + text("\u00a5Z", 255) + bytes([MAGIC0, MAGIC1]))
    hidden = frame(TYPES["pong"], 5, struct.pack("<I", 0xA55AA55A))
    carrier = frame(TYPES["log"], 6, bytes([2, len(hidden)]) + hidden)
    huge = frame(TYPES["pong"], 7, struct.pack("<I", 7))
    streams = [
        ("back-to-back", a + b + c),
        ("garbage-first", bytes([0x00, 0xA5, 0x11, 0x5A, 0xF4]) + a + b),
        ("boot-text-first", b"ESP-ROM:esp32s3-20210327\r\nwaiting for download\r\n" + c),
        ("bad-crc-then-good", bad_crc(a) + b + c),
        ("magic-inside-payload", tricky + a),
        ("frame-inside-corrupted-frame", bad_crc(carrier) + c),
        ("oversized-length-then-good", bytes([MAGIC0, MAGIC1, 0x84, 0x00, 0xFF, 0xFF]) + huge),
        ("double-magic", bytes([MAGIC0]) + a),
        ("trailing-partial", a + b[:5]),
    ]
    out = []
    for name, data in streams:
        frames, bad, dropped = run_parser(data, 1)
        bulk_frames, bulk_bad, bulk_dropped = run_parser(data, MAX_FRAME)
        assert frames == bulk_frames and bad == bulk_bad and dropped == bulk_dropped, name
        out.append({"name": name, "bytes": data.hex(), "frames": [f.hex() for f in frames], "badFrames": bad,
                    "dropped": dropped})
    return out


def tag_vectors() -> list[dict]:
    ping = frame(TYPES["ping"], 3, struct.pack("<I", 42))
    hello_req = frame(TYPES["hello_req"], 0, bytes([2]))
    long_log = frame(TYPES["log"], 8, bytes([3]) + text("x" * 200, 255))
    out = []
    for name, key, data in (("ping", KEY, ping), ("hello-request", bytes(32), hello_req),
                            ("long-log", hashlib.sha256(b"gamescrcpy").digest(), long_log)):
        tag = hmac.new(key, data, hashlib.sha256).digest()[:TAG_SIZE]
        out.append({"name": name, "key": key.hex(), "frame": data.hex(), "tag": tag.hex(),
                    "datagram": (data + tag).hex()})
    return out


def sha_vectors() -> list[dict]:
    samples = [b"", b"abc", b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", bytes(range(256)) * 3]
    return [{"data": sample.hex(), "sha256": hashlib.sha256(sample).hexdigest()} for sample in samples]


def crc_vectors() -> list[dict]:
    samples = [b"123456789", b"", b"\x00", bytes([0x02, 0x07, 0x01, 0x00, 0x00])]
    return [{"data": sample.hex(), "crc": crc16(sample)} for sample in samples]


def legacy_vectors() -> list[dict]:
    samples = [
        ("first-finger-down", 1, 0, 16384, 100, 1),
        ("second-finger-lift", 0, 1, 32767, 0, 2),
        ("corner", 1, 9, 0, 32767, 10),
    ]
    out = []
    for name, tip, contact, x, y, count in samples:
        packet = struct.pack("<BBBIIB", 0xF4, tip, contact, x, y, count)
        out.append({"name": name, "tip": tip, "id": contact, "x": x, "y": y, "count": count, "packet": packet.hex()})
    return out


def message_vectors() -> list[dict]:
    out = []
    for name, kind, seq, fields in MESSAGES:
        payload = ENCODERS[kind](fields)
        out.append({"name": name, "kind": kind, "type": TYPES[kind], "seq": seq, "fields": fields,
                    "payload": payload.hex(), "frame": frame(TYPES[kind], seq, payload).hex()})
    return out


def invalid_vectors() -> list[dict]:
    return [{"name": name, "kind": kind, "type": TYPES[kind], "payload": payload} for name, kind, payload in INVALID]


def main() -> None:
    document = {
        "format": "gamescrcpy.esp32-vectors",
        "protocol": 2,
        "crc": crc_vectors(),
        "sha256": sha_vectors(),
        "messages": message_vectors(),
        "invalidPayloads": invalid_vectors(),
        "invalidFrames": frame_vectors(),
        "streams": stream_vectors(),
        "tags": tag_vectors(),
        "legacy": legacy_vectors(),
    }
    out = Path(__file__).resolve().parent / "vectors.json"
    out.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
