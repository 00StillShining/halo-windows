"""The deck's silkscreen as a two-ink SDF (plan M7, assets/stage/ep40_face_sdf.htex):
parameter names, VOLUME, the LIVE and BPM / METRONOME brackets, GAIN and SWING,
the big X and Y and the fader tick, all cut from TE's front drawing.

The drawing also paints every key, plate, knob and the fader slot; those areas
belong to their own parts and are excluded, as is anything cream (the deck).
Channels: R green, G orange; 0.5 at the ink edge, +-RANGE_MM across 0..255.
The texture covers REGION (reference mm), row 0 at its port-side edge."""

import numpy as np

from . import inks, raster, sdf

HI_RES = 24          # px per mm for coverage
DENSITY = 8          # texels per mm (deck lettering is 2.1 mm caps with 0.3 mm strokes)
RANGE_MM = 1.0       # +-8 texels


def build(fills, region, exclude_rects=(), exclude_discs=()):
    """region (x0, y0, x1, y1) mm; exclusions as (x0, y0, x1, y1) and (cx, cy, r).
    -> (level 0 [h, w, 2] uint8, preview [h, w, 3] uint8, ink texels per channel)."""
    x0, y0, x1, y1 = region
    lab = raster.label_image(fills, region, HI_RES, inks.classify)
    h, w = lab.shape
    ys = y0 + (np.arange(h) + 0.5) / HI_RES
    xs = x0 + (np.arange(w) + 0.5) / HI_RES
    keep = np.ones((h, w), bool)
    for rx0, ry0, rx1, ry1 in exclude_rects:
        r = (ys >= ry0) & (ys <= ry1)
        c = (xs >= rx0) & (xs <= rx1)
        keep[np.ix_(r, c)] = False
    for cx, cy, rad in exclude_discs:
        keep &= ((xs[None, :] - cx) ** 2 + (ys[:, None] - cy) ** 2) > rad * rad
    factor = HI_RES // DENSITY
    th, tw = h // factor, w // factor
    out = np.zeros((th, tw, 2), np.uint8)
    preview = np.zeros((th, tw, 3), np.float32)
    preview[:] = (0xDB, 0xD0, 0xB9)
    ink_px = []
    for channel, ink in enumerate((inks.GREEN, inks.ORANGE)):
        mask = keep & (lab == ink)
        ink_px.append(int(mask.sum()))
        d = sdf.downsample_mean(sdf.signed_distance_mm(mask, HI_RES), factor)[:th, :tw]
        out[..., channel] = sdf.encode(d, RANGE_MM)
        cover = np.clip(d * DENSITY + 0.5, 0.0, 1.0)[..., None]
        rgb = np.array({inks.GREEN: (0x00, 0x50, 0x3A), inks.ORANGE: (0xFF, 0x68, 0x1D)}[ink], np.float32)
        preview = preview * (1 - cover) + rgb * cover
    return out, np.clip(preview, 0, 255).astype(np.uint8), ink_px
