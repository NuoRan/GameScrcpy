"""Compares the colors of two pictures of the same screen at the same relative positions.

    python tools/compare_patches.py device.png mirror.png --points "0.06,0.4 0.19,0.4" [--size 9]

Each point is a fraction of the width and of the height; the averages of a size x size square
around it in both pictures are printed with their largest channel difference. Used to check the
displayed colors against the phone's own screenshot (MED-001). Reads 8-bit RGB or RGBA PNGs
without interlacing; standard library only.
"""

import argparse
import struct
import sys
import zlib


def read_png(path: str) -> tuple[int, int, int, bytearray]:
    with open(path, "rb") as file:
        data = file.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    position, compressed = 8, bytearray()
    width = height = depth = color = interlace = 0
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break
    if depth != 8 or color not in (2, 6) or interlace:
        raise SystemExit(f"{path}: only 8-bit RGB or RGBA without interlacing")
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    pixels = bytearray(height * stride)
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if kind == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 255
        elif kind == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + (left + previous[i]) // 2) & 255
        elif kind == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        pixels[y * stride:(y + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def average(picture: tuple[int, int, int, bytearray], fx: float, fy: float, size: int) -> tuple[int, int, int]:
    width, height, channels, pixels = picture
    cx, cy = int(fx * width), int(fy * height)
    totals, count = [0, 0, 0], 0
    for y in range(max(0, cy - size // 2), min(height, cy + size // 2 + 1)):
        for x in range(max(0, cx - size // 2), min(width, cx + size // 2 + 1)):
            offset = (y * width + x) * channels
            for channel in range(3):
                totals[channel] += pixels[offset + channel]
            count += 1
    return tuple(round(total / count) for total in totals)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--points", required=True, help='space-separated "x,y" fractions')
    parser.add_argument("--size", type=int, default=9)
    args = parser.parse_args()
    reference, candidate = read_png(args.reference), read_png(args.candidate)
    worst = 0
    for point in args.points.split():
        fx, fy = (float(value) for value in point.split(","))
        expected, actual = average(reference, fx, fy, args.size), average(candidate, fx, fy, args.size)
        difference = max(abs(e - a) for e, a in zip(expected, actual))
        worst = max(worst, difference)
        print(f"{point:>12}  reference {expected}  candidate {actual}  difference {difference}")
    print(f"largest difference: {worst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
