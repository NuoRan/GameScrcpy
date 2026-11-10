#!/usr/bin/env python3
from __future__ import annotations

import argparse
import io
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

DESCRIPTION = (
    "Draws the GameScrcpy program icon (ADR-0089, revised 2026-11-07): a dark tile with a white phone lying on its "
    "side and an ember crosshair on its screen, flat and without effects. Every size is drawn on its own from signed "
    "distances at four times the size and reduced; 16 to 48 px use shapes on the pixel grid. Writes the .ico the "
    "program, its shortcuts and the installer use, and optionally a preview of every size on dark and light "
    "backgrounds and a large PNG. Needs numpy and Pillow."
)

UNITS = 256.0
SUPERSAMPLE = 4
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
TILE_CENTER = 128.0
TILE_HALF = 120.0
TILE_POWER = 5.0


def color(hex_code: str) -> np.ndarray:
    value = hex_code.lstrip("#")
    return np.array([int(value[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], dtype=np.float32)


TILE = ((0.0, color("#1B1D23")), (1.0, color("#0E0F12")))
EDGE = color("#FFFFFF")
EDGE_ALPHA = 0.09
PHONE = color("#F4F5F7")
MARK = color("#FF6B3D")


@dataclass(frozen=True)
class Look:
    phone: tuple[float, float, float, float, float]
    stroke: float
    ring: float = 0.0
    ring_stroke: float = 0.0
    ticks: tuple[float, float] | None = None
    dot: float = 0.0
    camera: float = 0.0


SMALL_LOOKS = {
    16: dict(phone=(8, 8, 5, 3, 1.5), stroke=2, dot=1.2),
    20: dict(phone=(10, 10, 7, 4, 2), stroke=2, dot=1.4),
    24: dict(phone=(12, 12, 8, 5, 2.5), stroke=2, ring=2.5, ring_stroke=1.25),
    32: dict(phone=(16, 16, 11, 7, 3.5), stroke=2, ring=3.4, ring_stroke=1.5, dot=0.9),
    40: dict(phone=(20, 20, 13.5, 8, 4), stroke=3, ring=4.2, ring_stroke=1.75, dot=1.05),
    48: dict(phone=(24, 24, 16, 9.5, 4.5), stroke=3, ring=5.0, ring_stroke=2.0, dot=1.25),
}


def look_for(size: int) -> Look:
    unit = UNITS / size
    if size in SMALL_LOOKS:
        spec = SMALL_LOOKS[size]
        return Look(phone=tuple(value * unit for value in spec["phone"]), stroke=spec["stroke"] * unit,
                    ring=spec.get("ring", 0.0) * unit, ring_stroke=spec.get("ring_stroke", 0.0) * unit,
                    dot=spec.get("dot", 0.0) * unit)
    return Look(phone=(128.0, 128.0, 84.0, 46.0, 24.0), stroke=max(12.0, 2.4 * unit), ring=13.0,
                ring_stroke=max(8.0, 1.7 * unit), ticks=(21.5, 28.0), camera=4.6 if size >= 96 else 0.0)


def gradient(stops: tuple[tuple[float, np.ndarray], ...], t: np.ndarray) -> np.ndarray:
    out = np.empty(t.shape + (3,), dtype=np.float32)
    positions = np.array([stop[0] for stop in stops], dtype=np.float32)
    for channel in range(3):
        out[..., channel] = np.interp(t, positions, [stop[1][channel] for stop in stops])
    return out


def mix(base: np.ndarray, paint: np.ndarray, amount: np.ndarray) -> np.ndarray:
    return base + (paint - base) * np.clip(amount, 0.0, 1.0)[..., None]


def rounded_box(x: np.ndarray, y: np.ndarray, box: tuple[float, float, float, float, float]) -> np.ndarray:
    cx, cy, half_width, half_height, radius = box
    qx = np.abs(x - cx) - (half_width - radius)
    qy = np.abs(y - cy) - (half_height - radius)
    outside = np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0))
    return outside + np.minimum(np.maximum(qx, qy), 0.0) - radius


def squircle(x: np.ndarray, y: np.ndarray) -> np.ndarray:
    u = np.abs(x - TILE_CENTER) / TILE_HALF
    v = np.abs(y - TILE_CENTER) / TILE_HALF
    norm = (u ** TILE_POWER + v ** TILE_POWER) ** (1.0 / TILE_POWER)
    safe = np.maximum(norm, 1e-3)
    slope = np.where(norm > 1e-3, np.hypot((u / safe) ** (TILE_POWER - 1.0), (v / safe) ** (TILE_POWER - 1.0)), 1.0)
    return TILE_HALF * (norm - 1.0) / np.maximum(slope, 0.5)


def segment(x: np.ndarray, y: np.ndarray, start: tuple[float, float], end: tuple[float, float]) -> np.ndarray:
    ax, ay = start
    ex, ey = end[0] - ax, end[1] - ay
    px, py = x - ax, y - ay
    h = np.clip((px * ex + py * ey) / (ex * ex + ey * ey), 0.0, 1.0)
    return np.hypot(px - ex * h, py - ey * h)


def crosshair(x: np.ndarray, y: np.ndarray, look: Look) -> np.ndarray | None:
    cx, cy = look.phone[0], look.phone[1]
    parts = []
    if look.ring > 0:
        parts.append(np.abs(np.hypot(x - cx, y - cy) - look.ring) - look.ring_stroke / 2)
    if look.ticks is not None:
        inner, outer = look.ticks
        for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
            parts.append(segment(x, y, (cx + dx * inner, cy + dy * inner), (cx + dx * outer, cy + dy * outer))
                         - look.ring_stroke / 2)
    if look.dot > 0:
        parts.append(np.hypot(x - cx, y - cy) - look.dot)
    return np.min(np.stack(parts), axis=0) if parts else None


def render(size: int, look: Look, supersample: int = SUPERSAMPLE) -> Image.Image:
    side = size * supersample
    scale = side / UNITS
    axis = (np.arange(side, dtype=np.float32) + 0.5) / scale
    x, y = np.meshgrid(axis, axis)

    def cover(distance: np.ndarray) -> np.ndarray:
        return np.clip(0.5 - distance * scale, 0.0, 1.0)

    tile = squircle(x, y)
    edge = max(1.0, UNITS / size)
    rgb = gradient(TILE, np.clip((y - (TILE_CENTER - TILE_HALF)) / (2 * TILE_HALF), 0.0, 1.0))
    rgb = mix(rgb, EDGE, cover(np.abs(tile + edge / 2) - edge / 2) * EDGE_ALPHA)
    rgb = mix(rgb, PHONE, cover(np.abs(rounded_box(x, y, look.phone)) - look.stroke / 2))
    mark = crosshair(x, y, look)
    if mark is not None:
        rgb = mix(rgb, MARK, cover(mark))
    if look.camera > 0:
        lens_x = look.phone[0] - look.phone[2] + look.stroke / 2 + 11.0
        rgb = mix(rgb, PHONE, cover(np.hypot(x - lens_x, y - look.phone[1]) - look.camera))

    alpha = cover(tile)
    layers = np.concatenate([rgb * alpha[..., None], alpha[..., None]], axis=2)
    reduced = layers.reshape(size, supersample, size, supersample, 4).mean(axis=(1, 3))
    opacity = reduced[..., 3:4]
    straight = np.where(opacity > 0, reduced[..., :3] / np.maximum(opacity, 1e-6), 0.0)
    pixels = np.concatenate([straight, opacity], axis=2)
    return Image.fromarray(np.round(pixels * 255.0).astype(np.uint8), "RGBA")


def dib(image: Image.Image) -> bytes:
    width, height = image.size
    flipped = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    pixels = flipped.tobytes("raw", "BGRA")
    alpha = flipped.getchannel("A").tobytes()
    stride = ((width + 31) // 32) * 4
    mask = bytearray()
    for row_index in range(height):
        row = bytearray(stride)
        for column in range(width):
            if alpha[row_index * width + column] == 0:
                row[column // 8] |= 0x80 >> (column % 8)
        mask += row
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, len(pixels) + len(mask), 0, 0, 0, 0)
    return header + pixels + bytes(mask)


def png(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def icon_file(images: dict[int, Image.Image]) -> bytes:
    sizes = sorted(images)
    blobs = [png(images[size]) if size >= 256 else dib(images[size]) for size in sizes]
    offset = 6 + 16 * len(sizes)
    directory = bytearray()
    for size, blob in zip(sizes, blobs):
        edge = 0 if size >= 256 else size
        directory += struct.pack("<BBBBHHII", edge, edge, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    return struct.pack("<HHH", 0, 1, len(sizes)) + bytes(directory) + b"".join(blobs)


def preview(images: dict[int, Image.Image]) -> Image.Image:
    sizes = sorted(images)
    gap = 16
    width = sum(sizes) + gap * (len(sizes) + 1)
    height = max(sizes) + 2 * gap
    backgrounds = ((0x1C, 0x1C, 0x1C, 255), (0xF3, 0xF3, 0xF3, 255), (0x5A, 0x6B, 0x8C, 255))
    sheet = Image.new("RGBA", (width, height * len(backgrounds)), (0, 0, 0, 255))
    for row, background in enumerate(backgrounds):
        sheet.paste(background, (0, row * height, width, (row + 1) * height))
        x = gap
        for size in sizes:
            sheet.alpha_composite(images[size], (x, row * height + gap + (max(sizes) - size)))
            x += size + gap
    return sheet


def main() -> int:
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument("--icon", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    parser.add_argument("--render", type=Path)
    parser.add_argument("--render-size", type=int, default=1024)
    args = parser.parse_args()
    images = {size: render(size, look_for(size)) for size in ICON_SIZES}
    args.icon.parent.mkdir(parents=True, exist_ok=True)
    args.icon.write_bytes(icon_file(images))
    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        preview(images).save(args.preview)
    if args.render:
        args.render.parent.mkdir(parents=True, exist_ok=True)
        render(args.render_size, look_for(max(ICON_SIZES)), 2).save(args.render)
    print(f"make_app_icon: {len(images)} sizes in {args.icon}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
