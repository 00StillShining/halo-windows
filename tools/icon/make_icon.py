"""Draws Halo's app icon in the redesign's language (docs/redesign/plan.md):
a graphite deck tile with the black glass set into it, the status strip's
ring glyph on the glass (faint ring + bright arc, an orange tip), and the
four OP-1 field encoder colours on the deck.

Small sizes (<= 32 px) get a simplified, bolder ring so they stay crisp.

Usage: tools/.venv/Scripts/python.exe -I tools/icon/make_icon.py <out_dir>
Writes halo-1024.png (detailed), halo-small.png (simplified, 256 px) and
halo.ico (16-256 px, each size drawn at its own resolution).
"""

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SS = 4  # supersampling

DECK_TOP, DECK_BOTTOM, DECK_EDGE = (0x1D, 0x1D, 0x20), (0x12, 0x12, 0x14), (0x2C, 0x2C, 0x30)
GLASS, BEZEL = (0x00, 0x00, 0x05), (0x26, 0x27, 0x2D)
INK = (0xF7, 0xF5, 0xF5)
ORANGE = (0xEF, 0x3E, 0x24)
ENCODERS = [(0x4D, 0x7E, 0xA9), (0xB3, 0x8E, 0x5E), (0xA7, 0xB1, 0xB8), (0xEF, 0x3E, 0x24)]


def arc_points(cx, cy, r, a0, a1, steps):
    # Angles in degrees, 0 = 12 o'clock, clockwise.
    pts = []
    for i in range(steps + 1):
        a = math.radians(a0 + (a1 - a0) * i / steps)
        pts.append((cx + r * math.sin(a), cy - r * math.cos(a)))
    return pts


def stroke_arc(d, cx, cy, r, a0, a1, width, colour):
    # One filled band (outer edge out, inner edge back): a polyline this thick
    # frays at every joint.
    outer = arc_points(cx, cy, r + width / 2, a0, a1, 720)
    inner = arc_points(cx, cy, r - width / 2, a0, a1, 720)
    d.polygon(outer + inner[::-1], fill=colour)
    if (a1 - a0) % 360 != 0:
        for x, y in (arc_points(cx, cy, r, a0, a0, 1)[0], arc_points(cx, cy, r, a1, a1, 1)[0]):  # round caps
            d.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=colour)


def draw(size, simple):
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # The deck tile: vertical graphite gradient, rounded, with a hairline edge.
    radius = S * 0.22
    tile = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    for y in range(S):
        t = y / (S - 1)
        c = tuple(int(DECK_TOP[i] + (DECK_BOTTOM[i] - DECK_TOP[i]) * t) for i in range(3))
        ImageDraw.Draw(tile).line([(0, y), (S, y)], fill=c + (255,))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius, fill=255)
    img.paste(tile, (0, 0), mask)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius, outline=DECK_EDGE, width=max(SS, int(S * 0.008)))

    if simple:
        # Small sizes: the glass fills the tile; one bold ring.
        inset = S * 0.10
        d.rounded_rectangle([inset, inset, S - inset, S - inset], S * 0.15, fill=GLASS)
        cx = cy = S / 2
        r, w = S * 0.25, S * 0.10
        stroke_arc(d, cx, cy, r, 0, 360, w, tuple(int(c * 0.30) for c in INK))
        stroke_arc(d, cx, cy, r, 0, 290, w, INK)
        tx, ty = arc_points(cx, cy, r, 290, 290, 1)[0]
        d.ellipse([tx - w * 0.62, ty - w * 0.62, tx + w * 0.62, ty + w * 0.62], fill=ORANGE)
    else:
        # The glass, set into the upper part of the deck.
        gx0, gy0, gx1, gy1 = S * 0.11, S * 0.11, S * 0.89, S * 0.72
        d.rounded_rectangle([gx0 - S * 0.008, gy0 - S * 0.008, gx1 + S * 0.008, gy1 + S * 0.008], S * 0.085, fill=BEZEL)
        d.rounded_rectangle([gx0, gy0, gx1, gy1], S * 0.075, fill=GLASS)

        # The ring glyph, centred on the glass.
        cx, cy = (gx0 + gx1) / 2, (gy0 + gy1) / 2
        r = (gy1 - gy0) * 0.30
        stroke_arc(d, cx, cy, r, 0, 360, S * 0.014, tuple(int(c * 0.32) for c in INK))
        stroke_arc(d, cx, cy, r, 0, 290, S * 0.034, INK)
        tx, ty = arc_points(cx, cy, r, 290, 290, 1)[0]
        dot = S * 0.030
        d.ellipse([tx - dot, ty - dot, tx + dot, ty + dot], fill=ORANGE)

        # Four encoder caps on the deck, OP-1 field colours.
        ky = S * 0.835
        kr = S * 0.052
        for i, c in enumerate(ENCODERS):
            kx = S * (0.245 + 0.17 * i)
            d.ellipse([kx - kr * 1.18, ky - kr * 1.18, kx + kr * 1.18, ky + kr * 1.18], fill=(0x0B, 0x0B, 0x0D))
            d.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=c)
            # The pointer line on each cap.
            a = math.radians(-35 + 30 * i)
            px, py = kx + kr * 0.72 * math.sin(a), ky - kr * 0.72 * math.cos(a)
            d.line([(kx, ky), (px, py)], fill=(0x06, 0x08, 0x0A) if i != 0 else INK, width=int(S * 0.012))

    return img.resize((size, size), Image.LANCZOS)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/icon")
    out.mkdir(parents=True, exist_ok=True)
    draw(1024, simple=False).save(out / "halo-1024.png")
    draw(256, simple=True).save(out / "halo-small.png")

    # Each ICO size is drawn at its own resolution: detailed from 48 px up.
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = [draw(s, simple=s <= 32) for s in sizes]
    frames[-1].save(out / "halo.ico", format="ICO", sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    print("wrote", out / "halo-1024.png", out / "halo-small.png", out / "halo.ico")


if __name__ == "__main__":
    main()
