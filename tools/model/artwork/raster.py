"""Painter's-order rasterisation of filled polygons with each path's own fill
rule (nonzero: summed signed coverage of its subpaths; evenodd: coverage
parity) into a label image of ink classes."""

import math

import numpy as np
from PIL import Image, ImageDraw


def _signed_area(poly):
    x, y = poly[:, 0], poly[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))


def label_image(fills, rect, px_per_mm, classify, background=0):
    """rect = (x0, y0, x1, y1) mm. Returns uint8 [h, w] of classify(colour) per
    pixel (the topmost fill wins); pixel (0, 0) covers (x0, y0)."""
    x0, y0, x1, y1 = rect
    w, h = int(round((x1 - x0) * px_per_mm)), int(round((y1 - y0) * px_per_mm))
    out = np.full((h, w), background, np.uint8)
    for f in fills:
        acc = None
        for poly, (bx0, by0, bx1, by1) in zip(f.subpaths, f.boxes):
            if bx1 < x0 or bx0 > x1 or by1 < y0 or by0 > y1:
                continue
            # Draw only the subpath's own pixel box (clipped to the image).
            c0 = max(0, int(math.floor((bx0 - x0) * px_per_mm)) - 1)
            r0 = max(0, int(math.floor((by0 - y0) * px_per_mm)) - 1)
            c1 = min(w, int(math.ceil((bx1 - x0) * px_per_mm)) + 2)
            r1 = min(h, int(math.ceil((by1 - y0) * px_per_mm)) + 2)
            if c1 <= c0 or r1 <= r0:
                continue
            img = Image.new("L", (c1 - c0, r1 - r0), 0)
            # PIL samples pixel centres at integer coordinates.
            pts = [((px - x0) * px_per_mm - 0.5 - c0, (py - y0) * px_per_mm - 0.5 - r0) for px, py in poly]
            ImageDraw.Draw(img).polygon(pts, fill=1)
            m = np.asarray(img, dtype=np.int16)
            if acc is None:
                acc = np.zeros((h, w), np.int16)
            acc[r0:r1, c0:c1] += m if f.evenodd else m * (1 if _signed_area(poly) >= 0 else -1)
        if acc is None:
            continue
        covered = (acc % 2 == 1) if f.evenodd else (acc != 0)
        out[covered] = classify(f.colour)
    return out
