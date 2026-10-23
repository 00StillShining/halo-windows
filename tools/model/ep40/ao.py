"""Baked ambient occlusion (plan D10, M8): a 0.1 mm height field of the rest-pose
device, then horizon-based occlusion from it, for every vertex (the u8 `ao`
byte) and for the deck as a model-XY texture (assets/stage/ep40_deck_ao.htex).
It replaces the per-frame height-map AO: the renderer does no AO work at all.

The device is a height field seen from above except for its walls; walls look
outward over the floor and, near a taller neighbour, up at it, which is what
the horizon test sees. The floor's own occlusion of low wall faces stays an
analytic term in part.frag, as before.

Coordinates: reference mm (x from the left, y from the port edge, z from the
bottom face); model metres convert with the same frames as build_ep40.py.
"""

import math

import numpy as np
from scipy import ndimage

RES = 0.1                                    # mm per height-field texel
REGION = (-8.0, -10.0, 184.0, 250.0)         # mm, beyond the footprint so the slider and walls fit
RADII = (0.3, 0.7, 1.3, 2.2, 3.5, 5.5, 8.5, 12.0)   # mm, horizon samples per direction
DIRECTIONS = 16
STRENGTH = 1.6                               # the old runtime contrast, baked in


def to_ref(p):
    """Model metres [..., 3] -> reference mm."""
    p = np.asarray(p, dtype=np.float64)
    return np.stack([p[..., 0] * 1000 + 88.0, 120.0 - p[..., 1] * 1000, p[..., 2] * 1000 + 8.0], axis=-1)


def height_field(triangles_ref):
    """Max z per texel over every triangle that faces up at all (mm), floor = 0."""
    x0, y0, x1, y1 = REGION
    w, h = int(round((x1 - x0) / RES)), int(round((y1 - y0) / RES))
    hf = np.zeros((h, w), np.float32)
    for tri in triangles_ref:
        a, b, c = tri
        e1, e2 = b - a, c - a
        nz = e1[0] * e2[1] - e1[1] * e2[0]
        if abs(nz) < 1e-9:                  # vertical in plan: its top edge is someone's top
            continue
        lo = np.floor((np.minimum(np.minimum(a, b), c)[:2] - (x0, y0)) / RES).astype(int)
        hi = np.ceil((np.maximum(np.maximum(a, b), c)[:2] - (x0, y0)) / RES).astype(int)
        lo = np.maximum(lo, 0)
        hi = np.minimum(hi, (w - 1, h - 1))
        if hi[0] < lo[0] or hi[1] < lo[1]:
            continue
        xs = x0 + (np.arange(lo[0], hi[0] + 1) + 0.5) * RES
        ys = y0 + (np.arange(lo[1], hi[1] + 1) + 0.5) * RES
        X, Y = np.meshgrid(xs, ys)
        # barycentric in plan
        d = nz
        l1 = ((X - a[0]) * e2[1] - (Y - a[1]) * e2[0]) / d
        l2 = ((Y - a[1]) * e1[0] - (X - a[0]) * e1[1]) / d
        inside = (l1 >= -1e-6) & (l2 >= -1e-6) & (l1 + l2 <= 1 + 1e-6)
        if not inside.any():
            continue
        z = a[2] + l1 * e1[2] + l2 * e2[2]
        sub = hf[lo[1]:hi[1] + 1, lo[0]:hi[0] + 1]
        np.maximum(sub, np.where(inside, z, 0.0).astype(np.float32), out=sub)
    return hf


class Horizon:
    def __init__(self, hf):
        self.levels = [ndimage.gaussian_filter(hf, sigma=max(r * 0.35 / RES, 0.5)) for r in RADII]

    def sample(self, level, x, y):
        x0, y0 = REGION[0], REGION[1]
        cx = (x - x0) / RES - 0.5
        cy = (y - y0) / RES - 0.5
        return ndimage.map_coordinates(self.levels[level], [cy, cx], order=1, mode="nearest")

    def ao(self, p, n):
        """p, n: [N, 3] reference mm / unit normals (reference axes: y toward the
        player, so plan directions are taken in reference x, y). -> [N] in 0..1."""
        base = p[:, :2] + n[:, :2] * 0.45
        occ = np.zeros(len(p))
        wsum = np.zeros(len(p))
        for i in range(DIRECTIONS):
            a = (i + 0.5) * 2 * math.pi / DIRECTIONS
            d = np.array([math.cos(a), math.sin(a)])
            w = np.clip(n[:, 2] + n[:, :2] @ d, 0.0, 1.0)
            slope = np.zeros(len(p))
            for level, r in enumerate(RADII):
                q = base + d * r
                hz = self.sample(level, q[:, 0], q[:, 1])
                slope = np.maximum(slope, (hz - p[:, 2]) / r)
            occ += w * slope / np.sqrt(1.0 + slope * slope)
            wsum += w
        raw = 1.0 - occ / np.maximum(wsum, 1e-4)
        return np.clip(1.0 - (1.0 - raw) * STRENGTH, 0.0, 1.0)


def deck_texture(horizon, hf, size=(1024, 1408), region=(0.0, 0.0, 176.0, 240.0)):
    """[h, w] uint8 AO of the top surface over `region` (row 0 at the port edge)."""
    w, h = size
    x0, y0, x1, y1 = region
    xs = x0 + (np.arange(w) + 0.5) * (x1 - x0) / w
    ys = y0 + (np.arange(h) + 0.5) * (y1 - y0) / h
    X, Y = np.meshgrid(xs, ys)
    pts = np.stack([X.ravel(), Y.ravel()], axis=1)
    rx0, ry0 = REGION[0], REGION[1]
    z = ndimage.map_coordinates(hf, [(pts[:, 1] - ry0) / RES - 0.5, (pts[:, 0] - rx0) / RES - 0.5], order=1, mode="nearest")
    p = np.column_stack([pts, z + 0.02])
    n = np.tile([0.0, 0.0, 1.0], (len(p), 1))
    out = np.empty(len(p))
    step = 200_000
    for i in range(0, len(p), step):
        out[i:i + step] = horizon.ao(p[i:i + step], n[i:i + step])
    return np.clip(np.round(out.reshape(h, w) * 255), 0, 255).astype(np.uint8)


def mips(level0):
    out = [level0]
    a = level0.astype(np.float32)
    while a.shape[0] > 1 or a.shape[1] > 1:
        h, w = max(1, a.shape[0] // 2), max(1, a.shape[1] // 2)
        fy, fx = a.shape[0] // h, a.shape[1] // w
        a = a[:h * fy, :w * fx].reshape(h, fy, w, fx).mean(axis=(1, 3))
        out.append(np.clip(np.round(a), 0, 255).astype(np.uint8))
    return out
