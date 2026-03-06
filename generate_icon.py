"""
Generate GameScrcpy app icon — Lucide-style gamepad (stroke-based)
matching the :/icons/gamepad.svg used in the main interface title.

Design: Transparent background + accent/white stroke gamepad
 - NO background fill — transparent PNG/ICO for clean taskbar appearance
 - Gamepad: accent (#6366f1) strokes + white (#FFFFFF) highlights
 - Matches the Lucide SVG: rounded rect body, D-pad cross, 2 action dots
"""
from PIL import Image, ImageDraw
import math, os

ACCENT       = (99, 102, 241)    # #6366f1
ACCENT_LIGHT = (165, 180, 252)   # lighter accent for highlight
WHITE        = (255, 255, 255)


def draw_rounded_rect(draw, xy, radius, fill=None, outline=None, width=1):
    """Draw a filled and/or stroked rounded rectangle."""
    x0, y0, x1, y1 = [int(v) for v in xy]
    r = int(min(radius, (x1 - x0) / 2, (y1 - y0) / 2))
    if r < 1:
        if fill:
            draw.rectangle([x0, y0, x1, y1], fill=fill, outline=outline, width=width)
        return

    if fill:
        draw.rectangle([x0 + r, y0, x1 - r, y1], fill=fill)
        draw.rectangle([x0, y0 + r, x1, y1 - r], fill=fill)
        draw.pieslice([x0, y0, x0 + 2*r, y0 + 2*r], 180, 270, fill=fill)
        draw.pieslice([x1 - 2*r, y0, x1, y0 + 2*r], 270, 360, fill=fill)
        draw.pieslice([x0, y1 - 2*r, x0 + 2*r, y1], 90, 180, fill=fill)
        draw.pieslice([x1 - 2*r, y1 - 2*r, x1, y1], 0, 90, fill=fill)

    if outline:
        lw = width
        draw.line([(x0 + r, y0), (x1 - r, y0)], fill=outline, width=lw)
        draw.line([(x0 + r, y1), (x1 - r, y1)], fill=outline, width=lw)
        draw.line([(x0, y0 + r), (x0, y1 - r)], fill=outline, width=lw)
        draw.line([(x1, y0 + r), (x1, y1 - r)], fill=outline, width=lw)
        draw.arc([x0, y0, x0 + 2*r, y0 + 2*r], 180, 270, fill=outline, width=lw)
        draw.arc([x1 - 2*r, y0, x1, y0 + 2*r], 270, 360, fill=outline, width=lw)
        draw.arc([x0, y1 - 2*r, x0 + 2*r, y1], 90, 180, fill=outline, width=lw)
        draw.arc([x1 - 2*r, y1 - 2*r, x1, y1], 0, 90, fill=outline, width=lw)


def generate_icon(size):
    """
    Replicate the Lucide gamepad.svg at the given pixel size.
    Transparent background — no dark square behind the gamepad.
    """
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # ---- Gamepad (Lucide-style, stroke-based) ----
    # Map SVG 24x24 coords to canvas with padding
    pad = size * 0.10
    game_size = size - 2 * pad
    scale = game_size / 24.0
    ox = pad
    oy = pad

    def sx(v):
        return ox + v * scale
    def sy(v):
        return oy + v * scale

    stroke_w = max(int(2.0 * scale + 0.5), 2)

    # Body: rounded rect from (2,6) to (22,18), rx=2
    body_rx = 2.0 * scale
    draw_rounded_rect(draw,
                       (sx(2), sy(6), sx(22), sy(18)),
                       body_rx,
                       fill=None, outline=ACCENT, width=stroke_w)
    # Fill body interior subtly
    fill_inset = stroke_w
    draw_rounded_rect(draw,
                       (sx(2) + fill_inset, sy(6) + fill_inset,
                        sx(22) - fill_inset, sy(18) - fill_inset),
                       max(body_rx - fill_inset, 0),
                       fill=(*ACCENT, 40))

    # D-pad cross (left side)
    draw.line([(sx(6), sy(12)), (sx(10), sy(12))], fill=WHITE, width=stroke_w)
    draw.line([(sx(8), sy(10)), (sx(8), sy(14))], fill=WHITE, width=stroke_w)

    # Action buttons (right side) — two filled dots
    dot_r = max(1.2 * scale, 1.5)
    draw.ellipse([sx(15) - dot_r, sy(13) - dot_r,
                  sx(15) + dot_r, sy(13) + dot_r], fill=WHITE)
    draw.ellipse([sx(18) - dot_r, sy(11) - dot_r,
                  sx(18) + dot_r, sy(11) + dot_r], fill=WHITE)

    return img


def main():
    out_dir = r"d:\QtScrcpy2\client\src\ui\resources"

    # Generate multiple sizes for ICO
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [generate_icon(sz) for sz in sizes]

    # Save ICO
    ico_path = os.path.join(out_dir, "GameScrcpy.ico")
    images[0].save(ico_path, format='ICO',
                   sizes=[(sz, sz) for sz in sizes],
                   append_images=images[1:])
    print(f"Created: {ico_path}")

    # Tray PNG 64x64
    logo_path = os.path.join(out_dir, "image", "tray", "logo.png")
    generate_icon(64).save(logo_path, format='PNG')
    print(f"Created: {logo_path}")

    # High-res PNG 512x512
    hires_path = os.path.join(out_dir, "icons", "app-icon.png")
    generate_icon(512).save(hires_path, format='PNG')
    print(f"Created: {hires_path}")

    print("Done!")


if __name__ == "__main__":
    main()
