"""Legend flicker over a slow orbit (plan M7 accept: `keys` at minimum radius,
30 frames in 1 degree steps, no legend edge flickers by more than 10 %).

A legend that is antialiased well keeps its printed "ink mass" (how much
darker or lighter than its cap it makes the tile) nearly constant while the
camera glides; an aliased one jumps as its edges snap between pixels. So each
legend tile of the first frame is tracked through the orbit (sub-pixel phase
correlation, frame to frame), its ink mass measured in every frame, and the
flicker is the frame-to-frame jitter of that mass around its smooth trend:
|m[n+1] - 2 m[n] + m[n-1]| / 2, relative to the mean mass.

    python tools/dev/orbit_flicker.py build/shots/flick [--tile 64] [--limit 0.10]

Frames are <shot>-<look>-yNN.png with their JSON sidecars (stage_shots.ps1
-Suffix yNN), in orbit order; shoot them in the dark look (the tile filter
leaves out tiles that reach the dark backdrop).
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage


def luma(path, rect):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float64) / 255.0
    x, y, w, h = rect
    return a[y:y + h, x:x + w] @ [0.2126, 0.7152, 0.0722]


def phase_shift(a, b):
    """(dy, dx) that moves b onto a, sub-pixel (parabolic peak)."""
    win = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    r = np.fft.fft2(a * win) * np.conj(np.fft.fft2(b * win))
    r /= np.maximum(np.abs(r), 1e-12)
    c = np.fft.ifft2(r).real
    py, px = np.unravel_index(np.argmax(c), c.shape)
    h, w = c.shape

    def sub(cm, c0, cp):
        d = cm - 2 * c0 + cp
        return 0.0 if abs(d) < 1e-12 else 0.5 * (cm - cp) / d
    dy = py + sub(c[(py - 1) % h, px], c[py, px], c[(py + 1) % h, px])
    dx = px + sub(c[py, (px - 1) % w], c[py, px], c[py, (px + 1) % w])
    return (dy - h if dy > h / 2 else dy), (dx - w if dx > w / 2 else dx)


def crop(img, cy, cx, t):
    """A t x t tile centred on a sub-pixel point."""
    iy, ix = int(np.floor(cy)), int(np.floor(cx))
    pad = t // 2 + 2
    if iy - pad < 0 or ix - pad < 0 or iy + pad >= img.shape[0] or ix + pad >= img.shape[1]:
        return None
    big = img[iy - pad:iy + pad, ix - pad:ix + pad]
    moved = ndimage.shift(big, (-(cy - iy), -(cx - ix)), order=3, mode="nearest")
    o = pad - t // 2
    return moved[o:o + t, o:o + t]


def ink_mass(tile):
    """Signed departure from the tile's cap level: legends darker or lighter."""
    cap = np.median(tile)
    return np.abs(tile - cap).sum()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--tile", type=int, default=64)
    ap.add_argument("--limit", type=float, default=0.10)
    a = ap.parse_args()
    frames = sorted(Path(a.folder).glob("*-y[0-9][0-9].png"))
    if len(frames) < 3:
        print("need at least three frames")
        return 2
    s = json.loads(frames[0].with_suffix(".json").read_text(encoding="utf-8-sig"))["stage"]
    imgs = [luma(f, (s["x"], s["y"], s["w"], s["h"])) for f in frames]
    t = a.tile
    h, w = imgs[0].shape

    # Legend tiles: strong local contrast in frame 0.
    starts = []
    for y in range(t, h - 2 * t, t // 2):
        for x in range(t, w - 2 * t, t // 2):
            tile = imgs[0][y:y + t, x:x + t]
            # On the device's face only: a tile reaching the dark backdrop tracks
            # the silhouette, which slides along itself (no legend there).
            if (tile < 0.12).mean() > 0.08:
                continue
            if np.percentile(tile, 98) - np.percentile(tile, 2) > 0.3:
                starts.append((y + t / 2, x + t / 2))

    results = []
    for cy, cx in starts:
        masses, ok = [], True
        prev = crop(imgs[0], cy, cx, t)
        for n in range(len(imgs)):
            tile = crop(imgs[n], cy, cx, t)
            if tile is None:
                ok = False
                break
            if n > 0:
                dy, dx = phase_shift(prev, tile)
                if abs(dy) > t / 3 or abs(dx) > t / 3:
                    ok = False
                    break
                cy, cx = cy - dy, cx - dx
                tile = crop(imgs[n], cy, cx, t)
                if tile is None:
                    ok = False
                    break
            masses.append(ink_mass(tile))
            prev = tile
        if not ok or len(masses) < 3:
            continue
        m = np.array(masses)
        jitter = np.abs(m[2:] - 2 * m[1:-1] + m[:-2]) / 2 / max(m.mean(), 1e-9)
        results.append((jitter.max(), np.median(jitter)))
    if not results:
        print("no legend tiles could be tracked")
        return 2
    worst = np.array([r[0] for r in results])
    p95 = float(np.percentile(worst, 95))
    print(f"{len(frames)} frames, {len(results)} legend tiles tracked: ink-mass jitter per step, "
          f"worst-step p95 over tiles {p95 * 100:.1f} %, median tile {np.median([r[1] for r in results]) * 100:.2f} %, "
          f"max {worst.max() * 100:.1f} %")
    ok = p95 <= a.limit
    print("-> " + ("ok" if ok else "FAIL") + f" (<= {a.limit * 100:.0f} %)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
