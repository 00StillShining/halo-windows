"""The keys' four-ink SDF atlas (plan section 4.5, assets/stage/ep40_keys_sdf.htex).

Each decal part (cap top, function plate, small key) gets a tile cut from TE's
front drawing at the part's own reference-mm rectangle, so a cap's UVs are a
planar projection of its rest-pose XY. Within a tile only ink inside the part's
include rectangle (its top, inset past the drawn key border) is kept, and only
ink that differs from the cap colour under it: the drawing paints each cap, then
its lettering.

Channels: R green, G orange, B white (cream lettering prints white), A charcoal.
Each is a signed distance, 0.5 at the ink edge, +-RANGE_MM across 0..255."""

from dataclasses import dataclass

import numpy as np

from . import inks, raster, sdf

HI_RES = 48          # px per mm for coverage
DENSITY = 12         # atlas texels per mm (the plan's 16 would double the asset for no visible gain)
RANGE_MM = 8 / 12    # +-8 texels at atlas density
MARGIN_MM = 0.25     # round each footprint; ink stays >= 0.6 mm inside it, so tiles need no gutter
ATLAS_W = 2048


@dataclass
class Tile:
    name: str
    rect: tuple          # part footprint (x0, y0, x1, y1), reference mm
    inset: tuple         # (x, y) mm from the footprint to the include rectangle
    origin: tuple = (0, 0)   # texel of the tile's (rect - margin) corner
    size: tuple = (0, 0)
    base: int = inks.NONE
    ink_px: dict = None


def _tile_size(rect):
    x0, y0, x1, y1 = rect
    return (int(round((x1 - x0 + 2 * MARGIN_MM) * DENSITY)), int(round((y1 - y0 + 2 * MARGIN_MM) * DENSITY)))


def pack(tiles):
    """Shelf packing, tallest first. Returns the atlas height (a multiple of 16)."""
    for t in tiles:
        t.size = _tile_size(t.rect)
    x = y = shelf = 0
    for t in sorted(tiles, key=lambda t: (-t.size[1], t.name)):
        w, h = t.size
        if x + w > ATLAS_W:
            x, y, shelf = 0, y + shelf, 0
        t.origin = (x, y)
        x += w
        shelf = max(shelf, h)
    height = y + shelf
    return (height + 15) // 16 * 16


def build(fills, tiles):
    """-> (atlas [H, W, 4] uint8, preview [H, W, 3] uint8). Fills tiles' base and ink_px."""
    height = pack(tiles)
    atlas = np.zeros((height, ATLAS_W, 4), np.uint8)   # 0 = far outside every ink
    preview = np.zeros((height, ATLAS_W, 3), np.uint8)
    factor = HI_RES // DENSITY
    for t in tiles:
        x0, y0, x1, y1 = t.rect
        tw, th = t.size
        # Exactly tw x th atlas texels (factor x that in coverage pixels).
        rect = (x0 - MARGIN_MM, y0 - MARGIN_MM, x0 - MARGIN_MM + tw / DENSITY, y0 - MARGIN_MM + th / DENSITY)
        lab = raster.label_image(fills, rect, HI_RES, inks.classify)
        h, w = lab.shape
        yy, xx = np.mgrid[0:h, 0:w]
        mx, my = rect[0] + (xx + 0.5) / HI_RES, rect[1] + (yy + 0.5) / HI_RES
        include = (mx >= x0 + t.inset[0]) & (mx <= x1 - t.inset[0]) & (my >= y0 + t.inset[1]) & (my <= y1 - t.inset[1])
        # The cap colour as drawn; NONE when the artwork paints only lettering
        # (neutral mode), so every ink is kept.
        counts = np.bincount(lab[include], minlength=6)
        t.base = int(counts.argmax())
        t.ink_px = {}
        ox, oy = t.origin
        tile_rgb = np.zeros((th, tw, 3), np.float32)
        base_rgb = _rgb(t.base)
        tile_rgb[:] = base_rgb
        for channel in range(4):
            members = [k for k, c in inks.CHANNEL.items() if c == channel and k != t.base]
            mask = include & np.isin(lab, members)
            t.ink_px[channel] = int(mask.sum())
            d = sdf.downsample_mean(sdf.signed_distance_mm(mask, HI_RES), factor)[:th, :tw]
            atlas[oy:oy + th, ox:ox + tw, channel] = sdf.encode(d, RANGE_MM)
            cover = np.clip(d * DENSITY + 0.5, 0.0, 1.0)[..., None]
            ink_rgb = _rgb({0: inks.GREEN, 1: inks.ORANGE, 2: inks.WHITE, 3: inks.CHARCOAL}[channel], as_ink=True)
            tile_rgb = tile_rgb * (1 - cover) + np.array(ink_rgb, np.float32) * cover
        preview[oy:oy + th, ox:ox + tw] = np.clip(tile_rgb, 0, 255).astype(np.uint8)
    return atlas, preview


def _rgb(ink, as_ink=False):
    if as_ink and ink in inks.INK_SRGB:
        h = inks.INK_SRGB[ink].lstrip("#")
    else:
        h = {inks.GREEN: "00503A", inks.ORANGE: "FF681D", inks.WHITE: "EEEFEA", inks.CHARCOAL: "0F0E12",
             inks.CREAM: "DBD0B9"}.get(ink, "808080")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def uv(tile, atlas_height, x_mm, y_mm):
    """Atlas UV of a reference-mm point on a tile's part (v = 0 at the first stored row)."""
    u = (tile.origin[0] + (x_mm - tile.rect[0] + MARGIN_MM) * DENSITY) / ATLAS_W
    v = (tile.origin[1] + (y_mm - tile.rect[1] + MARGIN_MM) * DENSITY) / atlas_height
    return u, v
