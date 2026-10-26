"""Draws Halo's app icon: an EP-40 Riddim keycap in the device's own cream,
green and orange, with the Halo ring printed on it like one of TE's key
legends (green ring, orange live segment), an orange status LED, and a small
"halo" legend in the corner the way the EP-40 labels its keys.

Every ICO size is drawn at its own resolution: from 48 px up the full keycap;
32 px and below a flatter cap with a bolder ring and no LED or text, so the
small sizes stay crisp.

Usage: tools/.venv/Scripts/python.exe -I tools/icon/make_icon.py <out_dir>
Writes halo-1024.png, halo-small.png (the simplified cap, 256 px) and
halo.ico (16-256 px).
"""

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
LEGEND_FONT = ROOT / "assets" / "fonts" / "LexendExa-Medium.ttf"

CREAM_HI, CREAM, CREAM_EDGE = (0xF7, 0xF3, 0xE9), (0xE6, 0xE0, 0xD0), (0xD9, 0xD1, 0xBD)
SKIRT_TOP, SKIRT_BOTTOM = (0xC9, 0xC0, 0xAA), (0xA2, 0x98, 0x80)
GREEN = (0x0A, 0x4A, 0x36)
ORANGE = (0xFA, 0x5A, 0x16)
LED_HOT = (0xFF, 0xB0, 0x80)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def vgrad(size, top, bottom):
    w, h = size
    g = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(g)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(top, bottom, y / max(1, h - 1)) + (255,))
    return g


def rrect_mask(size, box, r, blur=0.0):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, r, fill=255)
    return m.filter(ImageFilter.GaussianBlur(blur)) if blur else m


def ellipse_mask(size, box, blur=0.0):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).ellipse(box, fill=255)
    return m.filter(ImageFilter.GaussianBlur(blur)) if blur else m


def tint(size, colour, mask, opacity=1.0):
    layer = Image.new("RGBA", size, colour + (0,))
    layer.putalpha(mask.point(lambda v: round(v * opacity)) if opacity != 1.0 else mask)
    return layer


def arc_band(d, cx, cy, r, w, a0, a1, fill, caps=True):
    # One filled band (a thick polyline frays at its joints); 0 deg = 12 o'clock.
    def pts(rr):
        return [(cx + rr * math.sin(math.radians(a0 + (a1 - a0) * i / 720)),
                 cy - rr * math.cos(math.radians(a0 + (a1 - a0) * i / 720))) for i in range(721)]
    d.polygon(pts(r + w / 2) + pts(r - w / 2)[::-1], fill=fill)
    if caps and (a1 - a0) % 360:
        for a in (a0, a1):
            x, y = cx + r * math.sin(math.radians(a)), cy - r * math.cos(math.radians(a))
            d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=fill)


def draw(size, simple):
    ss = 4 if size <= 64 else 2
    S = size * ss
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Body: the key sits a little high so its drop shadow fits inside the tile.
    m = S * (0.07 if not simple else 0.04)
    body = (m, m * 0.7, S - m, S - m * 1.3)
    r = S * (0.21 if not simple else 0.2)
    if not simple:
        shadow = rrect_mask((S, S), (body[0], body[1] + S * 0.03, body[2], body[3] + S * 0.03), r, blur=S * 0.028)
        img = Image.alpha_composite(img, tint((S, S), (0, 0, 0), shadow, 0.42))

    # The skirt (the cap's darker side walls) and the face inset into it,
    # higher at the front edge like a keycap seen from slightly above.
    img.paste(vgrad((S, S), SKIRT_TOP, SKIRT_BOTTOM), (0, 0), rrect_mask((S, S), body, r))
    skirt = S * (0.034 if not simple else 0.03)
    face = (body[0] + skirt, body[1] + skirt * 0.55, body[2] - skirt, body[3] - skirt * 2.2)
    img.paste(vgrad((S, S), CREAM_HI, CREAM), (0, 0), rrect_mask((S, S), face, r * 0.84))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(face, r * 0.84, outline=CREAM_EDGE, width=max(1, round(S * 0.004)))
    if not simple:
        # A soft sheen across the top of the face.
        sheen = rrect_mask((S, S), (face[0] + S * 0.04, face[1] + S * 0.015, face[2] - S * 0.04, face[1] + S * 0.16), r * 0.6, blur=S * 0.035)
        img = Image.alpha_composite(img, tint((S, S), (255, 255, 255), sheen, 0.45))
        d = ImageDraw.Draw(img)

    # The printed Halo ring: green, with the orange live segment.
    cx, cy = (face[0] + face[2]) / 2, (face[1] + face[3]) / 2 + (S * 0.012 if not simple else 0)
    R = (face[2] - face[0]) * (0.27 if not simple else 0.29)
    w = S * (0.052 if not simple else 0.1)
    arc_band(d, cx, cy, R, w, 0, 360, GREEN, caps=False)
    arc_band(d, cx, cy, R, w, 292, 360, ORANGE)

    if not simple:
        # The status LED: a lens with a soft glow, top-left as on the EP-40.
        lx, ly, lr = face[0] + S * 0.085, face[1] + S * 0.082, S * 0.024
        glow = ellipse_mask((S, S), (lx - lr * 3.2, ly - lr * 3.2, lx + lr * 3.2, ly + lr * 3.2), blur=lr * 1.4)
        # The glow lights the face only, never past the key's edge.
        from PIL import ImageChops
        glow = ImageChops.multiply(glow, rrect_mask((S, S), face, r * 0.84))
        img = Image.alpha_composite(img, tint((S, S), ORANGE, glow, 0.38))
        d = ImageDraw.Draw(img)
        d.ellipse((lx - lr, ly - lr, lx + lr, ly + lr), fill=ORANGE)
        hr = lr * 0.42
        d.ellipse((lx - lr * 0.45 - hr, ly - lr * 0.45 - hr, lx - lr * 0.45 + hr, ly - lr * 0.45 + hr), fill=LED_HOT)

        # The key legend, bottom right, in the EP-40's small print.
        if size >= 128:
            font = ImageFont.truetype(str(LEGEND_FONT), round(S * 0.062))
            text = "halo"
            tb = d.textbbox((0, 0), text, font=font)
            tx = face[2] - S * 0.07 - (tb[2] - tb[0])
            ty = face[3] - S * 0.065 - (tb[3] - tb[1]) - tb[1]
            d.text((tx, ty), text, font=font, fill=GREEN)

    return img.resize((size, size), Image.LANCZOS)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/icon")
    out.mkdir(parents=True, exist_ok=True)
    draw(1024, simple=False).save(out / "halo-1024.png")
    draw(256, simple=True).save(out / "halo-small.png")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = [draw(s, simple=s <= 32) for s in sizes]
    frames[-1].save(out / "halo.ico", format="ICO", sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    print("wrote", out / "halo-1024.png", out / "halo-small.png", out / "halo.ico")


if __name__ == "__main__":
    main()
