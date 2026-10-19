"""Masked shot diffs and pixel probes for the UI redesign (plan section 6.1).

Run with the tools venv, isolated:  tools\\.venv\\Scripts\\python -I tools/dev/ui_probe.py ...

  diff A.png B.png [--mask x,y,w,h ...] [--tol 2] [--area 0.1]
      Per-channel tolerance `tol`; fails when more than `area` percent of the
      unmasked pixels differ. Masks exclude the GL stage, the event log, clocks.
  probe IMG.png --rect x,y,w,h --rgb FAB413 [--tol 24] [--min 5]
      Fails unless at least `min` percent of the rect is within `tol` of rgb
      (e.g. the honest slot's MOCK yellow, the caution band).
  absent IMG.png --rect x,y,w,h --rgb DC1E5A [--tol 20] [--max 0.2]
      Fails if more than `max` percent of the rect is that colour (e.g. no
      crimson where no blocker should be).
  greys IMG.png --rect x,y,w,h [--max 1]
      Crispness check for pixel-font cells: fails if more than `max` percent of
      the rect's pixels are neither near-ink nor near-background.

Coordinates are in the PNG's own pixels (a shot includes the window frame).
"""

import argparse
import sys

from PIL import Image, ImageChops


def rect(s):
    x, y, w, h = (int(float(v)) for v in s.split(","))
    return (x, y, x + w, y + h)


def rgb(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def near(p, c, tol):
    return all(abs(a - b) <= tol for a, b in zip(p[:3], c))


def pixels(img):
    get = getattr(img, "get_flattened_data", None)
    return list(get() if get else img.getdata())


def fraction(img, box, pred):
    region = img.crop(box).convert("RGB")
    px = pixels(region)
    if not px:
        return 0.0
    return 100.0 * sum(1 for p in px if pred(p)) / len(px)


def cmd_diff(a):
    ia = Image.open(a.a).convert("RGB")
    ib = Image.open(a.b).convert("RGB")
    if ia.size != ib.size:
        print(f"diff: size differs {ia.size} vs {ib.size}")
        return 1
    diff = ImageChops.difference(ia, ib)
    for m in a.mask or []:
        diff.paste((0, 0, 0), rect(m))
    px = pixels(diff)
    bad = sum(1 for p in px if max(p) > a.tol)
    pct = 100.0 * bad / len(px)
    ok = pct <= a.area
    print(f"diff: {pct:.3f}% of pixels differ by more than {a.tol} ({'ok' if ok else 'FAIL'}, limit {a.area}%)")
    return 0 if ok else 1


def cmd_probe(a):
    img = Image.open(a.img)
    c = rgb(a.rgb)
    pct = fraction(img, rect(a.rect), lambda p: near(p, c, a.tol))
    ok = pct >= a.min
    print(f"probe: {pct:.2f}% of {a.rect} is #{a.rgb} ({'ok' if ok else 'FAIL'}, need {a.min}%)")
    return 0 if ok else 1


def cmd_absent(a):
    img = Image.open(a.img)
    c = rgb(a.rgb)
    pct = fraction(img, rect(a.rect), lambda p: near(p, c, a.tol))
    ok = pct <= a.max
    print(f"absent: {pct:.2f}% of {a.rect} is #{a.rgb} ({'ok' if ok else 'FAIL'}, max {a.max}%)")
    return 0 if ok else 1


def cmd_greys(a):
    img = Image.open(a.img).convert("RGB")
    box = rect(a.rect)
    region = img.crop(box)
    px = pixels(region)
    lum = [round(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) for p in px]
    lo, hi = min(lum), max(lum)
    mids = sum(1 for v in lum if lo + 24 < v < hi - 24)
    pct = 100.0 * mids / max(1, len(lum))
    ok = pct <= a.max
    print(f"greys: {pct:.2f}% intermediate pixels in {a.rect} ({'ok' if ok else 'FAIL'}, max {a.max}%)")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("diff")
    d.add_argument("a"); d.add_argument("b")
    d.add_argument("--mask", action="append")
    d.add_argument("--tol", type=int, default=2)
    d.add_argument("--area", type=float, default=0.1)
    for name in ("probe", "absent"):
        p = sub.add_parser(name)
        p.add_argument("img")
        p.add_argument("--rect", required=True)
        p.add_argument("--rgb", required=True)
        p.add_argument("--tol", type=int, default=24 if name == "probe" else 20)
        if name == "probe":
            p.add_argument("--min", type=float, default=5.0)
        else:
            p.add_argument("--max", type=float, default=0.2)
    g = sub.add_parser("greys")
    g.add_argument("img")
    g.add_argument("--rect", required=True)
    g.add_argument("--max", type=float, default=1.0)
    a = ap.parse_args()
    return {"diff": cmd_diff, "probe": cmd_probe, "absent": cmd_absent, "greys": cmd_greys}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
