"""Signed distance fields from high-resolution coverage (plan section 4.5):
0.5 is the ink edge, inside is above 0.5, and 0..1 spans +-range_mm."""

import numpy as np
from scipy import ndimage


def signed_distance_mm(mask, px_per_mm):
    """Positive inside the ink, in mm, at the mask's resolution."""
    if not mask.any():
        return np.full(mask.shape, -1e3, np.float32)
    inside = ndimage.distance_transform_edt(mask)
    outside = ndimage.distance_transform_edt(~mask)
    # Pixel-centre distances: the edge lies half a pixel between in and out.
    return ((np.where(mask, inside - 0.5, 0.5 - outside)) / px_per_mm).astype(np.float32)


def downsample_mean(a, factor):
    h, w = a.shape[0] // factor * factor, a.shape[1] // factor * factor
    return a[:h, :w].reshape(h // factor, factor, w // factor, factor).mean(axis=(1, 3))


def encode(d_mm, range_mm):
    return np.clip(np.round((0.5 + d_mm / (2.0 * range_mm)) * 255.0), 0, 255).astype(np.uint8)


def mips(level0):
    """Box-filtered mip chain of an [h, w, c] uint8 SDF (averaged distances), to 1 x 1."""
    out = [level0]
    a = level0.astype(np.float32)
    while a.shape[0] > 1 or a.shape[1] > 1:
        h, w = max(1, a.shape[0] // 2), max(1, a.shape[1] // 2)
        fy, fx = a.shape[0] // h, a.shape[1] // w
        a = a[:h * fy, :w * fx].reshape(h, fy, w, fx, -1).mean(axis=(1, 3))
        out.append(np.clip(np.round(a), 0, 255).astype(np.uint8))
    return out
