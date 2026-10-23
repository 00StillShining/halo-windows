"""Legend-edge flicker over a slow orbit (plan M7 accept: `keys` at minimum
radius, 30 frames in 1 degree steps, no legend-edge pixel flickers by more than
10 %).

Frames n and n+1 differ by a 1 degree yaw, so each tile of frame n+1 is first
aligned to frame n (phase correlation, sub-pixel). What is left at strong
edges, relative to the tile's local contrast, is flicker: a legend edge that
jumps between pixel states rather than gliding.

    python tools/dev/orbit_flicker.py build/shots/flick --tile 96 --limit 0.10
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
    a = a[y:y + h, x:x + w]
    return a @ [0.2126, 0.7152, 0.0722]


def phase_shift(a, b):
    """(dy, dx) that moves b onto a, sub-pixel (parabolic peak)."""
    win = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    fa, fb = np.fft.fft2(a * win), np.fft.fft2(b * win)
    r = fa * np.conj(fb)
    r /= np.maximum(np.abs(r), 1e-12)
    c = np.fft.ifft2(r).real
    py, px = np.unravel_index(np.argmax(c), c.shape)

    def sub(cm, c0, cp):
        d = cm - 2 * c0 + cp
        return 0.0 if abs(d) < 1e-12 else 0.5 * (cm - cp) / d
    h, w = c.shape
    dy = py + sub(c[(py - 1) % h, px], c[py, px], c[(py + 1) % h, px])
    dx = px + sub(c[py, (px - 1) % w], c[py, px], c[py, (px + 1) % w])
    if dy > h / 2:
        dy -= h
    if dx > w / 2:
        dx -= w
    return dy, dx


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--tile", type=int, default=96)
    ap.add_argument("--limit", type=float, default=0.10)
    a = ap.parse_args()
    frames = sorted(Path(a.folder).glob("*-y[0-9][0-9].png"))
    if len(frames) < 2:
        print("need at least two frames")
        return 2
    meta = json.loads(frames[0].with_suffix(".json").read_text(encoding="utf-8-sig"))
    s = meta["stage"]
    rect = (s["x"], s["y"], s["w"], s["h"])
    imgs = [luma(f, rect) for f in frames]
    t = a.tile
    worst, samples = [], []
    for n in range(len(imgs) - 1):
        A, B = imgs[n], imgs[n + 1]
        for y in range(0, A.shape[0] - t, t):
            for x in range(0, A.shape[1] - t, t):
                ta, tb = A[y:y + t, x:x + t], B[y:y + t, x:x + t]
                contrast = np.percentile(ta, 98) - np.percentile(ta, 2)
                if contrast < 0.25:          # no legend here (flat deck, background)
                    continue
                dy, dx = phase_shift(ta, tb)
                if abs(dy) > t / 4 or abs(dx) > t / 4:
                    continue
                tb_aligned = ndimage.shift(B, (dy, dx), order=3, mode="nearest")[y:y + t, x:x + t]
                grad = np.hypot(ndimage.sobel(ta, 0), ndimage.sobel(ta, 1))
                edges = grad > np.percentile(grad, 90)
                inner = np.zeros_like(edges)
                inner[4:-4, 4:-4] = True
                edges &= inner
                if edges.sum() < 20:
                    continue
                d = np.abs(ta - tb_aligned)[edges] / contrast
                samples.append(np.percentile(d, 95))
                worst.append((np.percentile(d, 99), n, x, y))
    if not samples:
        print("no legend tiles found")
        return 2
    samples = np.array(samples)
    worst.sort(reverse=True)
    p95 = float(np.percentile(samples, 95))
    print(f"{len(frames)} frames, {len(samples)} tile pairs: edge change after alignment, "
          f"p95 of tiles {p95 * 100:.1f} % of local contrast (median {np.median(samples) * 100:.1f} %)")
    for v, n, x, y in worst[:5]:
        print(f"  worst: frame {n}->{n + 1} tile ({x},{y}) p99 {v * 100:.1f} %")
    ok = p95 <= a.limit
    print("-> " + ("ok" if ok else "FAIL") + f" (<= {a.limit * 100:.0f} %)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
