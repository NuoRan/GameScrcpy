import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "data" / "vision"
EXAMPLES = ROOT / "examples" / "tasks"
PICTURES = (
    ("find-and-tap", "notifications.png", "template_notifications_row.pgm"),
    ("wait-then-tap", "network.png", "template_network_row.pgm"),
    ("daily-check-in", "check-in.png", "template_battery_row.pgm"),
    ("daily-check-in", "done.png", "template_search_bar.pgm"),
)
SIGNATURE = b"\x89PNG\r\n\x1a\n"
FULL_RANGE = bytes(min(255, max(0, round((value - 16) * 255 / 219))) for value in range(256))


def read_pgm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    fields: list[bytes] = []
    at = 0
    while len(fields) < 4:
        while at < len(data) and data[at:at + 1].isspace():
            at += 1
        start = at
        while at < len(data) and not data[at:at + 1].isspace():
            at += 1
        if start == at:
            raise ValueError(f"{path.name}: the header ends early")
        fields.append(data[start:at])
    if fields[0] != b"P5" or fields[3] != b"255":
        raise ValueError(f"{path.name}: not an 8-bit binary PGM")
    width, height = int(fields[1]), int(fields[2])
    pixels = data[at + 1:at + 1 + width * height]
    if width <= 0 or height <= 0 or len(pixels) != width * height:
        raise ValueError(f"{path.name}: {width}x{height} does not match its {len(pixels)} bytes of pixels")
    return width, height, pixels


def scanlines(width: int, height: int, pixels: bytes) -> bytes:
    return b"".join(b"\x00" + pixels[row * width:(row + 1) * width] for row in range(height))


def chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def png(width: int, height: int, pixels: bytes) -> bytes:
    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    stream = zlib.compress(scanlines(width, height, pixels), 9)
    return SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", stream) + chunk(b"IEND", b"")


def decoded(path: Path) -> tuple[int, int, bytes] | None:
    if not path.is_file():
        return None
    data = path.read_bytes()
    if not data.startswith(SIGNATURE):
        return None
    at = len(SIGNATURE)
    header = b""
    stream = b""
    while at + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            header = body
        elif kind == b"IDAT":
            stream += body
        elif kind == b"IEND":
            break
    if len(header) != 13:
        return None
    width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", header)
    if (depth, color, compression, filtering, interlace) != (8, 0, 0, 0, 0):
        return None
    try:
        return width, height, zlib.decompress(stream)
    except zlib.error:
        return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Writes the pictures of the example tasks in examples/tasks as grayscale PNG files, from the "
                    "vision fixtures in tests/data/vision that the demo recording emulator_h265.media shows.")
    parser.add_argument("--check", action="store_true", help="fail when a picture is missing or out of date")
    arguments = parser.parse_args()
    written = 0
    stale: list[Path] = []
    for example, name, fixture in PICTURES:
        try:
            width, height, luma = read_pgm(FIXTURES / fixture)
        except (OSError, ValueError) as error:
            print(f"make_example_images: {error}", file=sys.stderr)
            return 1
        pixels = luma.translate(FULL_RANGE)
        target = EXAMPLES / example / "res" / name
        if decoded(target) == (width, height, scanlines(width, height, pixels)):
            continue
        if arguments.check:
            stale.append(target)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(png(width, height, pixels))
        written += 1
    for target in stale:
        print(f"make_example_images: {target.relative_to(ROOT).as_posix()} is out of date; run "
              f"python tools/make_example_images.py", file=sys.stderr)
    if stale:
        return 1
    if not arguments.check:
        print(f"make_example_images: {len(PICTURES)} pictures, {written} written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
