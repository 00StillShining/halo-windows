"""Halo icon renderer: SVG sources -> review sheets and the shipped icon files.

An icon is two SVGs on a 1024x1024 viewBox:
  master.svg  - the full design, used for 40 px and up
  small.svg   - a simplified design for 32 px and below (taskbar, title bar)

Usage (run with tools/.venv/Scripts/python.exe -I):
  render.py sheet <master.svg> <small.svg> <out.png>
      A review sheet: every Windows icon size on a dark taskbar, a light
      taskbar and a desktop-like mid grey, plus 6x nearest-neighbour zooms of
      16/24/32 px so pixel-level crispness can be judged.
  render.py ico <master.svg> <small.svg> <out_dir>
      Writes halo-1024.png (master), halo-small.png (small, 256 px) and
      halo.ico (16, 20, 24, 32 from small; 40, 48, 64, 128, 256 from master).
"""

import io
import sys
from pathlib import Path

import resvg_py
from PIL import Image, ImageDraw

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SMALL_MAX = 32


def render(svg_path, size):
    svg = Path(svg_path).read_text(encoding="utf-8")
    png = bytes(resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size))
    return Image.open(io.BytesIO(png)).convert("RGBA")


def frame(master, small, size):
    return render(small if size <= SMALL_MAX else master, size)


def sheet(master, small, out):
    sizes = [256, 128, 64, 48, 40, 32, 24, 20, 16]
    backgrounds = [("dark taskbar", (0x20, 0x20, 0x22)), ("light taskbar", (0xF3, 0xF3, 0xF3)),
                   ("desktop", (0x3A, 0x55, 0x70))]
    pad, label_h = 24, 22
    row_w = pad + sum(s + pad for s in sizes)
    zoom_sizes, zoom = [32, 24, 16], 6
    zoom_w = pad + sum(s * zoom + pad for s in zoom_sizes)
    W = max(row_w, zoom_w)
    row_h = 256 + 2 * pad + label_h
    H = row_h * len(backgrounds) + (32 * zoom + 2 * pad + label_h)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    d = ImageDraw.Draw(img)
    frames = {s: frame(master, small, s) for s in set(sizes) | set(zoom_sizes)}
    y = 0
    for name, bg in backgrounds:
        d.rectangle([0, y, W, y + row_h - 1], fill=bg + (255,))
        ink = (0xEE, 0xEE, 0xEE) if sum(bg) < 382 else (0x22, 0x22, 0x22)
        d.text((pad, y + 4), name, fill=ink)
        x = pad
        for s in sizes:
            img.alpha_composite(frames[s], (x, y + label_h + pad + (256 - s) // 2))
            d.text((x, y + row_h - 16), str(s), fill=ink)
            x += s + pad
        y += row_h
    d.rectangle([0, y, W, H], fill=(0x20, 0x20, 0x22, 255))
    d.text((pad, y + 4), "6x zoom, dark taskbar (pixel crispness)", fill=(0xEE, 0xEE, 0xEE))
    x = pad
    for s in zoom_sizes:
        z = frames[s].resize((s * zoom, s * zoom), Image.NEAREST)
        img.alpha_composite(z, (x, y + label_h + pad))
        x += s * zoom + pad
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    img.save(out)


def ico(master, small, out_dir):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    render(master, 1024).save(out / "halo-1024.png")
    render(small, 256).save(out / "halo-small.png")
    frames = [frame(master, small, s) for s in ICO_SIZES]
    frames[-1].save(out / "halo.ico", format="ICO", sizes=[(s, s) for s in ICO_SIZES], append_images=frames[:-1])


def main(argv):
    if len(argv) != 5 or argv[1] not in ("sheet", "ico"):
        print(__doc__)
        return 2
    (sheet if argv[1] == "sheet" else ico)(argv[2], argv[3], argv[4])
    print("wrote", argv[4])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
