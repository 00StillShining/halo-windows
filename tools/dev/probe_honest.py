"""Honesty probes over a folder of ui_shots PNGs (plan section 6.1):
  - owner decision (2026-10-22): MOCK runs carry NO marking in the UI, so
    there is no hazard yellow in the strip's honest slot or around the
    stage, in any mode, size or scale. (PREVIEW, red blockers, orange notes
    and the write confirms are unchanged.)

Run isolated with the tools venv:
  tools\\.venv\\Scripts\\python -I tools/dev/probe_honest.py build/shots/ui/r4

Shot names follow ui_shots.ps1: m<mode>-<w>x<h>[-s<scale>][-suffix].png.
The window frame around the client area is measured from the image size.
"""

import pathlib
import re
import sys

from PIL import Image

MOCK = (0xFA, 0xB4, 0x13)
NAME = re.compile(r"m(\d)-(\d+)x(\d+)(?:-s(\d+))?")


def near(p, c, tol=28):
    return all(abs(a - b) <= tol for a, b in zip(p[:3], c))


def fraction(img, box):
    region = img.crop(box)
    get = getattr(region, "get_flattened_data", None)
    px = list(get() if get else region.getdata())
    return 100.0 * sum(1 for p in px if near(p, MOCK)) / max(1, len(px))


def main():
    folder = pathlib.Path(sys.argv[1])
    failures = 0
    for path in sorted(folder.glob("m*.png")):
        m = NAME.match(path.stem)
        if not m:
            continue
        mode, w, h = int(m.group(1)), int(m.group(2)), int(m.group(3))
        scale = {None: 1.0, "125": 1.25, "15": 1.5}[m.group(4)]
        img = Image.open(path).convert("RGB")
        border = (img.width - w * scale) / 2.0
        top = img.height - h * scale - border

        def box(x, y, bw, bh):
            return (int(border + x * scale), int(top + y * scale), int(border + (x + bw) * scale), int(top + (y + bh) * scale))

        slot = fraction(img, box(110, 14, 110, 22))
        ok = slot < 0.5
        line = f"{path.name}: honest slot {slot:.1f}% yellow"
        if mode == 1:
            band_y = 50 + 22 + 30
            band = fraction(img, box(24, band_y, 8, 160))
            line += f", stage band {band:.1f}% yellow"
            ok = ok and band < 0.5
        print(("ok   " if ok else "FAIL ") + line)
        failures += 0 if ok else 1
    print(f"probe_honest: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
