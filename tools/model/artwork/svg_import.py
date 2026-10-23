"""TE's front drawing (reference/te-ep40/69033ff8...svg) as filled polygons in
reference millimetres: x from the left edge, y from the port edge toward the
player (the drawing's own orientation), scaled so the outline is 176 x 240 mm.

Stroked outlines (key borders, knob rings, screw circles, the drawing frame)
are dropped: only fills are artwork (plan D9)."""

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from svgelements import SVG, Arc, Close, CubicBezier, Line, Move, QuadraticBezier, Shape
from svgelements import Path as SvgPath

FRONT_SVG = Path(__file__).resolve().parents[3] / "reference" / "te-ep40" / "69033ff84b343523886b5edb.svg"
OUTLINE_SIZE_UNITS = (289.0, 394.0)   # the cream body fill: 176 x 240 mm
WIDTH_MM = 176.0


@dataclass
class Fill:
    index: int                 # document order (painter's order)
    colour: str                # "#rrggbb"
    evenodd: bool
    subpaths: list = field(default_factory=list)   # [N, 2] float arrays, mm
    boxes: list = field(default_factory=list)      # (x0, y0, x1, y1) per subpath, mm


def _flatten(subpath, steps=16):
    pts = []
    for seg in subpath:
        if isinstance(seg, Move):
            pts.append((seg.end.x, seg.end.y))
        elif isinstance(seg, (Line, Close)):
            if seg.end is not None:
                pts.append((seg.end.x, seg.end.y))
        elif isinstance(seg, (CubicBezier, QuadraticBezier, Arc)):
            for p in seg.npoint(np.linspace(0.0, 1.0, steps + 1)[1:]):
                pts.append((float(p[0]), float(p[1])))
    return np.array(pts, dtype=np.float64) if len(pts) >= 3 else None


def _fill_hex(e):
    if e.fill is None or e.fill.value is None:
        return None
    s = str(e.fill).lower()
    return None if s == "none" else s


def load(path=FRONT_SVG):
    """-> (fills in painter's order, (origin_x, origin_y, mm_per_unit))."""
    svg = SVG.parse(str(path))
    shapes = [e for e in svg.elements() if isinstance(e, Shape)]
    origin = None
    for e in shapes:
        bb = SvgPath(e).bbox()
        if bb and _fill_hex(e) == "#dbd0b9" and abs((bb[2] - bb[0]) - OUTLINE_SIZE_UNITS[0]) < 0.5 \
                and abs((bb[3] - bb[1]) - OUTLINE_SIZE_UNITS[1]) < 0.5:
            origin = (bb[0], bb[1], WIDTH_MM / (bb[2] - bb[0]))
            break
    if origin is None:
        raise SystemExit(f"{path}: no 289 x 394 cream outline found")
    ox, oy, s = origin
    fills = []
    for i, e in enumerate(shapes):
        colour = _fill_hex(e)
        if colour is None:
            continue
        rule = str(e.values.get("fill-rule", "nonzero")).lower()
        f = Fill(i, colour, rule == "evenodd")
        for sub in SvgPath(e).as_subpaths():
            poly = _flatten(sub)
            if poly is None:
                continue
            poly = (poly - (ox, oy)) * s
            f.subpaths.append(poly)
            f.boxes.append((*poly.min(axis=0), *poly.max(axis=0)))
        if f.subpaths:
            fills.append(f)
    return fills, origin
