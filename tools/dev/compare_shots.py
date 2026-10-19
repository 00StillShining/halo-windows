"""Measures and compares 3D stage shots from tools/dev/stage_shots.ps1.

Every shot is a PNG of the whole window plus a JSON sidecar giving the stage
rectangle inside it, so all measurements look at the stage only.

  python -I tools/dev/compare_shots.py diff  build/shots/m0 build/shots/m1 [--out build/shots/m1/diff]
  python -I tools/dev/compare_shots.py card  build/shots/m1/card-paper.png
  python -I tools/dev/compare_shots.py mask  build/shots/m1/mask-paper.png
  python -I tools/dev/compare_shots.py same  a.png b.png [--tol 1]      (determinism: max per-pixel delta)
  python -I tools/dev/compare_shots.py border shot.png --colour #AEB9BE (stage border equals the token)
  python -I tools/dev/compare_shots.py hist  shot.png                    (backdrop banding: empty histogram bins)
  python -I tools/dev/compare_shots.py fringe shot.png                   (silhouette edges between device and floor)
  python -I tools/dev/compare_shots.py sheet out.png a.png b.png ... [--ref reference/te-ep40/c_keys.png ...]

Exit status is non-zero when a check with a threshold fails.
"""

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

# Plan section 2.5: the albedo each card swatch should show (sRGB hex).
CARD_TARGETS = {
    "deck": "#DCD2BF",
    "body": "#DCD2BF",
    "shell": "#0A4A36",
    "key": "#E9E9E5",
    "orange": "#FA5A16",
    "green": "#0A4A36",
    "plate": "#ECEBE6",
    "frame": "#0B2E24",
    "knobx": "#FA5A16",
}


# ------------------------------------------------------------------ loading
def load(path):
    path = Path(path)
    img = np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)
    side = path.with_suffix(".json")
    meta = {}
    if side.exists():
        meta = json.loads(side.read_text(encoding="utf-8-sig"))
    return img, meta


def stage_crop(img, meta):
    s = (meta or {}).get("stage")
    if not s:
        return img
    x, y, w, h = s["x"], s["y"], s["w"], s["h"]
    return img[y:y + h, x:x + w]


def hex_rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float64)


# ------------------------------------------------------------------ colour
def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def rgb_to_lab(rgb):
    lin = srgb_to_linear(rgb)
    m = np.array([[0.4124564, 0.3575761, 0.1804375],
                  [0.2126729, 0.7151522, 0.0721750],
                  [0.0193339, 0.1191920, 0.9503041]])
    xyz = lin @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > (6 / 29) ** 3, np.cbrt(xyz), xyz / (3 * (6 / 29) ** 2) + 4 / 29)
    L = 116 * f[..., 1] - 16
    a = 500 * (f[..., 0] - f[..., 1])
    b = 200 * (f[..., 1] - f[..., 2])
    return np.stack([L, a, b], axis=-1)


def ciede2000(lab1, lab2):
    L1, a1, b1 = np.moveaxis(np.asarray(lab1, dtype=np.float64), -1, 0)
    L2, a2, b2 = np.moveaxis(np.asarray(lab2, dtype=np.float64), -1, 0)
    C1, C2 = np.hypot(a1, b1), np.hypot(a2, b2)
    Cm = (C1 + C2) / 2
    G = 0.5 * (1 - np.sqrt(Cm ** 7 / (Cm ** 7 + 25 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = np.hypot(a1p, b1), np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360
    dLp = L2 - L1
    dCp = C2p - C1p
    dh = h2p - h1p
    dh = np.where(C1p * C2p == 0, 0, np.where(dh > 180, dh - 360, np.where(dh < -180, dh + 360, dh)))
    dHp = 2 * np.sqrt(C1p * C2p) * np.sin(np.radians(dh) / 2)
    Lpm = (L1 + L2) / 2
    Cpm = (C1p + C2p) / 2
    hsum = h1p + h2p
    hpm = np.where(C1p * C2p == 0, hsum,
                   np.where(np.abs(h1p - h2p) <= 180, hsum / 2,
                            np.where(hsum < 360, (hsum + 360) / 2, (hsum - 360) / 2)))
    T = (1 - 0.17 * np.cos(np.radians(hpm - 30)) + 0.24 * np.cos(np.radians(2 * hpm))
         + 0.32 * np.cos(np.radians(3 * hpm + 6)) - 0.20 * np.cos(np.radians(4 * hpm - 63)))
    dtheta = 30 * np.exp(-(((hpm - 275) / 25) ** 2))
    Rc = 2 * np.sqrt(Cpm ** 7 / (Cpm ** 7 + 25 ** 7))
    Sl = 1 + 0.015 * (Lpm - 50) ** 2 / np.sqrt(20 + (Lpm - 50) ** 2)
    Sc = 1 + 0.045 * Cpm
    Sh = 1 + 0.015 * Cpm * T
    Rt = -np.sin(np.radians(2 * dtheta)) * Rc
    return np.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2 + Rt * (dCp / Sc) * (dHp / Sh))


def delta_e(rgb1, rgb2):
    return float(ciede2000(rgb_to_lab(np.asarray(rgb1, float)), rgb_to_lab(np.asarray(rgb2, float))))


# ------------------------------------------------------------------ checks
def cmd_card(args):
    img, meta = load(args.shot)
    st = stage_crop(img, meta)
    log = Path(args.shot).with_suffix(".log")
    names = []
    if log.exists():
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            if "card:" in line:
                names = [tok.split("=")[0] for tok in line.split("card:")[1].split()]
    if not names:
        print("no card line in", log)
        return 2
    h = st.shape[0]
    n = len(names)
    worst = 0.0
    failed = False
    for i, name in enumerate(names):
        y0, y1 = int(h * (i + 0.3) / n), int(h * (i + 0.7) / n)
        sample = st[y0:y1, st.shape[1] // 4: 3 * st.shape[1] // 4].reshape(-1, 3).mean(axis=0)
        target = CARD_TARGETS.get(name)
        if target is None:
            print(f"  {name:8s} shown #{''.join(f'{int(round(c)):02X}' for c in sample)}  (no target)")
            continue
        de = delta_e(sample, hex_rgb(target))
        limit = args.limits.get(name, args.limit) if args.limits else args.limit
        ok = de <= limit
        failed |= not ok
        worst = max(worst, de)
        print(f"  {name:8s} shown #{''.join(f'{int(round(c)):02X}' for c in sample)}  target {target}  dE2000 {de:5.2f}"
              f"  {'ok' if ok else 'FAIL'} (<= {limit})")
    print(f"card: worst dE2000 {worst:.2f}")
    return 1 if failed else 0


def device_box(img, meta):
    st = stage_crop(img, meta)
    lit = st.max(axis=2) > 40
    ys, xs = np.nonzero(lit)
    if len(xs) == 0:
        return None, st.shape
    return (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1), st.shape


def cmd_mask(args):
    failed = False
    for shot in args.shots:
        img, meta = load(shot)
        box, shape = device_box(img, meta)
        h, w = shape[:2]
        if box is None:
            print(f"{shot}: no device pixels")
            failed = True
            continue
        x0, y0, x1, y1 = box
        margins = dict(left=x0 / w, right=(w - x1) / w, top=y0 / h, bottom=(h - y1) / h)
        bw, bh = (x1 - x0) / w, (y1 - y0) / h
        fill = max(bw, bh)  # the limiting dimension is the one the device fills most
        ok = min(margins.values()) >= args.margin and fill >= args.fill
        failed |= not ok
        print(f"{Path(shot).name}: stage {w}x{h} box {x0},{y0}-{x1},{y1} margins "
              + " ".join(f"{k} {v * 100:.1f}%" for k, v in margins.items())
              + f" fill w {bw * 100:.1f}% h {bh * 100:.1f}% -> {'ok' if ok else 'FAIL'}"
              + f" (margin >= {args.margin * 100:.0f}%, limiting fill >= {args.fill * 100:.0f}%)")
    return 1 if failed else 0


def cmd_same(args):
    a, ma = load(args.a)
    b, mb = load(args.b)
    a, b = stage_crop(a, ma).astype(int), stage_crop(b, mb).astype(int)
    if a.shape != b.shape:
        print("shapes differ", a.shape, b.shape)
        return 1
    d = np.abs(a - b).max(axis=2)
    print(f"max delta {d.max()} / 255, pixels over {args.tol}: {(d > args.tol).sum()} of {d.size}")
    return 0 if d.max() <= args.tol else 1


def cmd_diff(args):
    a_dir, b_dir = Path(args.a), Path(args.b)
    out = Path(args.out) if args.out else b_dir / "diff"
    out.mkdir(parents=True, exist_ok=True)
    for pb in sorted(b_dir.glob("*.png")):
        pa = a_dir / pb.name
        if not pa.exists():
            continue
        a, ma = load(pa)
        b, mb = load(pb)
        a, b = stage_crop(a, ma), stage_crop(b, mb)
        if a.shape != b.shape:
            print(f"{pb.name}: size changed {a.shape[:2]} -> {b.shape[:2]}")
            continue
        de = ciede2000(rgb_to_lab(a.astype(float)), rgb_to_lab(b.astype(float)))
        heat = np.clip(de / 10.0, 0, 1)
        vis = (np.stack([heat, heat * 0.3, 1 - heat], axis=-1) * 255).astype(np.uint8)
        Image.fromarray(np.concatenate([a, b, vis], axis=1)).save(out / pb.name)
        print(f"{pb.name}: mean dE {de.mean():.2f}, p99 {np.percentile(de, 99):.2f}, max {de.max():.2f}")
    return 0


def cmd_border(args):
    img, meta = load(args.shot)
    st = stage_crop(img, meta).astype(int)
    target = hex_rgb(args.colour).astype(int)
    ring = np.concatenate([st[:args.width].reshape(-1, 3), st[-args.width:].reshape(-1, 3),
                           st[:, :args.width].reshape(-1, 3), st[:, -args.width:].reshape(-1, 3)])
    d = np.abs(ring - target).max(axis=1)
    print(f"border vs {args.colour}: max channel delta {d.max()}, mean {d.mean():.2f}, "
          f"exact {(d == 0).mean() * 100:.1f}%, within 1: {(d <= 1).mean() * 100:.1f}%")
    return 0 if d.max() <= args.tol else 1


def cmd_hist(args):
    img, meta = load(args.shot)
    st = stage_crop(img, meta)
    if args.mask:
        m, mm = load(args.mask)
        device = stage_crop(m, mm).max(axis=2) > 40
        # keep only the backdrop well away from the device and its shadow
        ys, xs = np.nonzero(device)
        keep = np.ones(device.shape, bool)
        if len(xs):
            pad = args.pad
            keep[max(ys.min() - pad, 0):ys.max() + pad, max(xs.min() - pad, 0):xs.max() + pad] = False
        px = st[keep]
    else:
        px = st.reshape(-1, 3)
    failed = False
    for c, name in enumerate("RGB"):
        v = px[:, c]
        lo, hi = int(v.min()), int(v.max())
        counts = np.bincount(v, minlength=256)[lo:hi + 1]
        empty = int((counts == 0).sum())
        failed |= empty > 0
        print(f"{name}: range {lo}-{hi}, empty bins inside the range: {empty}")
    return 1 if failed else 0


def cmd_fringe(args):
    """Silhouette edges: every edge pixel lies between the device and the floor
    values next to it, channel by channel (no dark or bright halo)."""
    img, meta = load(args.shot)
    m, mm = load(args.mask)
    st = stage_crop(img, meta).astype(int)
    device = stage_crop(m, mm).max(axis=2) > 127
    from scipy import ndimage
    # Compare each silhouette pixel with its immediate neighbours: the device
    # 2-4 px inside and the floor 2-4 px outside. (Interior shading further in,
    # like a bevel's highlight, is not a fringe.)
    inner = ndimage.binary_erosion(device, iterations=2) & ~ndimage.binary_erosion(device, iterations=4)
    outer = ndimage.binary_dilation(device, iterations=4) & ~ndimage.binary_dilation(device, iterations=2)
    edge = ndimage.binary_dilation(device, iterations=1) & ~ndimage.binary_erosion(device, iterations=1)
    ys, xs = np.nonzero(edge)
    bad = 0
    total = 0
    r = 4
    for y, x in zip(ys[::args.step], xs[::args.step]):
        y0, y1, x0, x1 = max(y - r, 0), y + r + 1, max(x - r, 0), x + r + 1
        win = st[y0:y1, x0:x1]
        di, do = inner[y0:y1, x0:x1], outer[y0:y1, x0:x1]
        if di.sum() == 0 or do.sum() == 0:
            continue
        dev = win[di]
        flo = win[do]
        lo = np.minimum(dev.min(axis=0), flo.min(axis=0)) - args.tol
        hi = np.maximum(dev.max(axis=0), flo.max(axis=0)) + args.tol
        total += 1
        if np.any(st[y, x] < lo) or np.any(st[y, x] > hi):
            bad += 1
    print(f"fringe: {bad} of {total} sampled edge pixels outside the device/floor range (tol {args.tol})")
    return 0 if bad <= args.allow * max(total, 1) else 1


def cmd_sheet(args):
    tiles = []
    for p in args.shots:
        img, meta = load(p)
        tiles.append((Path(p).stem, Image.fromarray(stage_crop(img, meta))))
    for p in args.ref or []:
        tiles.append(("ref " + Path(p).stem, Image.open(p).convert("RGB")))
    cell_w = args.cell
    rows = []
    for label, im in tiles:
        scale = cell_w / im.width
        im = im.resize((cell_w, max(1, int(im.height * scale))), Image.LANCZOS)
        rows.append((label, im))
    cols = args.cols
    n_rows = math.ceil(len(rows) / cols)
    row_h = [max(im.height for _, im in rows[r * cols:(r + 1) * cols]) + 24 for r in range(n_rows)]
    sheet = Image.new("RGB", (cols * (cell_w + 8) + 8, sum(row_h) + 8 * n_rows + 8), (40, 40, 40))
    draw = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(rows):
        r = i // cols
        x = 8 + (i % cols) * (cell_w + 8)
        y = 8 + sum(row_h[:r]) + 8 * r
        sheet.paste(im, (x, y + 22))
        draw.text((x + 2, y + 4), label, fill=(235, 235, 235))
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out)
    print("wrote", args.out)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("card")
    s.add_argument("shot")
    s.add_argument("--limit", type=float, default=5.0)
    s.add_argument("--limits", type=lambda t: {k: float(v) for k, v in (kv.split("=") for kv in t.split(","))},
                   default=None, help="per swatch, e.g. deck=3,orange=3")
    s.set_defaults(func=cmd_card)

    s = sub.add_parser("mask")
    s.add_argument("shots", nargs="+")
    s.add_argument("--margin", type=float, default=0.05)
    s.add_argument("--fill", type=float, default=0.60)
    s.set_defaults(func=cmd_mask)

    s = sub.add_parser("same")
    s.add_argument("a")
    s.add_argument("b")
    s.add_argument("--tol", type=int, default=1)
    s.set_defaults(func=cmd_same)

    s = sub.add_parser("diff")
    s.add_argument("a")
    s.add_argument("b")
    s.add_argument("--out")
    s.set_defaults(func=cmd_diff)

    s = sub.add_parser("border")
    s.add_argument("shot")
    s.add_argument("--colour", required=True)
    s.add_argument("--width", type=int, default=2)
    s.add_argument("--tol", type=int, default=0)
    s.set_defaults(func=cmd_border)

    s = sub.add_parser("hist")
    s.add_argument("shot")
    s.add_argument("--mask")
    s.add_argument("--pad", type=int, default=120)
    s.set_defaults(func=cmd_hist)

    s = sub.add_parser("fringe")
    s.add_argument("shot")
    s.add_argument("--mask", required=True)
    s.add_argument("--tol", type=int, default=2)
    s.add_argument("--step", type=int, default=3)
    s.add_argument("--allow", type=float, default=0.01)
    s.set_defaults(func=cmd_fringe)

    s = sub.add_parser("sheet")
    s.add_argument("out")
    s.add_argument("shots", nargs="+")
    s.add_argument("--ref", nargs="*")
    s.add_argument("--cell", type=int, default=560)
    s.add_argument("--cols", type=int, default=3)
    s.set_defaults(func=cmd_sheet)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
