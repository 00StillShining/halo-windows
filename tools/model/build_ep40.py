"""The EP-40 model compiler (docs/redesign/riddim-3d-plan.md section 4), first
stage: the stepped chassis, the keys and their apertures, and the knobs are
built from ep40/spec.py; every other part is carried over from the Blender
model (usdz_to_hmesh.py) onto the surface it sits on (spec.CARRY_OVER). Key,
plate and small-key legends are decals: TE's front drawing cut into the keys'
SDF atlas (artwork/atlas.py), sampled through each cap's planar UVs; the old
geometric legends are dropped.

    python -I tools/model/build_ep40.py <EP40.usdz> --out assets/stage/EP40.hmesh
        [--atlas assets/stage/ep40_keys_sdf.htex] [--face assets/stage/ep40_face_sdf.htex]
        [--preview build/model/keys_atlas.png]

The deck's silkscreen comes from the same drawing (artwork/face.py).

Model space: Z up, metres, origin at the footprint centre, ports at +Y.
"""

import argparse
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "ep40"))
import spec  # noqa: E402
import usdz_to_hmesh as legacy  # noqa: E402
from artwork import atlas as keys_atlas, face as face_atlas, svg_import  # noqa: E402

# The deck silkscreen texture's extent (reference mm): the step to the front edge.
# The face texture's extent (reference mm): from the label plate's top edge to the
# front edge. Above the step only the label plate is printed (LABEL_PLATE).
FACE_REGION = (0.0, 0.0, spec.WIDTH.mm, spec.DEPTH.mm)
# The label plate as drawn (svg-path: the #EEEFEA fill, 0..119.55 x 8.57..56.50 mm).
LABEL_PLATE = (0.0, 8.57, 119.55, 56.50)
# The fader's slot as drawn (svg-px): the 3D track covers it; its tick stays printed.
FADER_SLOT = (14.2, 168.0, 17.3, 208.5)


# ------------------------------------------------------------------ frames
def X(x_mm): return (x_mm - 88.0) / 1000.0
def Y(y_mm): return (120.0 - y_mm) / 1000.0
def Z(z_mm): return (z_mm - 8.0) / 1000.0


DECK = Z(spec.DECK_TOP.mm)
UPPER_TOP_MM = spec.UPPER_TOP.mm
UPPER = Z(spec.UPPER_TOP.mm)
# Satin cap plastic: low enough that the 0.7 mm top fillet catches a line of
# the studio key (plan section 1, "soft 0.7 mm edges that catch a line of light").
CAP_ROUGHNESS = 0.30


# ------------------------------------------------------------------ geometry
def rounded_outline(hx, hy, r, steps=8):
    """Points and outward normals round a rounded rectangle (corner centres at
    +-(hx - r), +-(hy - r)), counter-clockwise."""
    out = []
    for cx, cy, start in ((hx - r, hy - r, 0.0), (-hx + r, hy - r, 90.0), (-hx + r, -hy + r, 180.0), (hx - r, -hy + r, 270.0)):
        for i in range(steps + 1):
            a = math.radians(start + 90.0 * i / steps)
            out.append((cx, cy, math.cos(a), math.sin(a)))
    return out


def keycap(cx, cy, w, h, z0, z1, w_top, h_top, corner, fillet, steps=8, fillet_steps=8):
    """A moulded key: walls tapering from (w, h) at z0 to (w_top, h_top), a
    quarter-round top fillet, a flat top. Triangles with analytic normals."""
    hx, hy = w / 2, h / 2
    inset = ((w - w_top) / 2 + (h - h_top) / 2) / 2      # the taper, per side
    outline = rounded_outline(hx, hy, corner, steps)
    wall_top = z1 - fillet
    slope = math.atan2(inset, wall_top - z0)              # walls lean in
    rings = [(0.0, z0, math.cos(slope), math.sin(slope)), (inset, wall_top, math.cos(slope), math.sin(slope))]
    for i in range(1, fillet_steps + 1):
        t = math.radians(90.0 * i / fillet_steps)
        a = slope + (math.pi / 2 - slope) * i / fillet_steps
        rings.append((inset + fillet * (1 - math.cos(t)), wall_top + fillet * math.sin(t), math.cos(a), math.sin(a)))

    def point(o, d, z):
        cxo, cyo, nx, ny = o
        r = max(corner - d, 0.0002)
        return (cx + cxo + nx * r, cy + cyo + ny * r, z)

    tris = []
    n = len(outline)
    for k in range(len(rings) - 1):
        d0, z0_, no0, nz0 = rings[k]
        d1, z1_, no1, nz1 = rings[k + 1]
        for i in range(n):
            j = (i + 1) % n
            a0, b0 = point(outline[i], d0, z0_), point(outline[j], d0, z0_)
            a1, b1 = point(outline[i], d1, z1_), point(outline[j], d1, z1_)
            na0 = (outline[i][2] * no0, outline[i][3] * no0, nz0)
            nb0 = (outline[j][2] * no0, outline[j][3] * no0, nz0)
            na1 = (outline[i][2] * no1, outline[i][3] * no1, nz1)
            nb1 = (outline[j][2] * no1, outline[j][3] * no1, nz1)
            tris.append(((a0, b0, b1), (na0, nb0, nb1)))
            tris.append(((a0, b1, a1), (na0, nb1, na1)))
    d_top, z_top = rings[-1][0], rings[-1][1]
    centre = (cx, cy, z_top)
    up = (0.0, 0.0, 1.0)
    for i in range(n):
        j = (i + 1) % n
        tris.append(((centre, point(outline[i], d_top, z_top), point(outline[j], d_top, z_top)), (up, up, up)))
    return tris


def frame(cx, cy, w_in, h_in, gap, z, corner_in, steps=8):
    """A flat dark ring round an aperture: the visible gap between key and deck."""
    inner = rounded_outline(w_in / 2, h_in / 2, corner_in, steps)
    tris = []
    up = (0.0, 0.0, 1.0)
    n = len(inner)
    for i in range(n):
        j = (i + 1) % n

        def p(o, r):
            return (cx + o[0] + o[2] * r, cy + o[1] + o[3] * r, z)

        a0, b0 = p(inner[i], corner_in), p(inner[j], corner_in)
        a1, b1 = p(inner[i], corner_in + gap), p(inner[j], corner_in + gap)
        tris.append(((a0, b0, b1), (up, up, up)))
        tris.append(((a0, b1, a1), (up, up, up)))
    return tris


def cylinder(cx, cy, z0, z1, radius, fillet):
    return [((tuple(p) for p in ps), (tuple(n) for n in ns)) for ps, ns in
            legacy.disc(cx, cy, z0, z1, radius, fillet, segments=48, fillet_steps=4)]


def slab(x0, x1, y0, y1, z0, z1, corner, top_fillet, bottom_fillet):
    hx, hy = (x1 - x0) / 2, (y1 - y0) / 2
    return legacy.rounded_slab(hx, hy, corner, z0, z1, top_fillet, bottom_fillet, (x0 + x1) / 2, (y0 + y1) / 2)


def ref_slab(rect, z0_mm, z1_mm, corner_mm=0.3, fillet_mm=0.0):
    """A slab over a reference-mm rectangle (x0, y0, x1, y1), z in reference mm."""
    x0, y0, x1, y1 = rect
    return slab(X(x0), X(x1), Y(y1), Y(y0), Z(z0_mm), Z(z1_mm), corner_mm / 1000, fillet_mm / 1000, 0.0)


def ref_quad(rect, z_mm):
    """An upward-facing rectangle at z (two triangles, counter-clockwise from above)."""
    x0, y0, x1, y1 = rect
    a, b, c, d = (X(x0), Y(y1), Z(z_mm)), (X(x1), Y(y1), Z(z_mm)), (X(x1), Y(y0), Z(z_mm)), (X(x0), Y(y0), Z(z_mm))
    up = (0.0, 0.0, 1.0)
    return [((a, b, c), (up, up, up)), ((a, c, d), (up, up, up))]


# ------------------------------------------------------------------ parts
class Builder:
    def __init__(self):
        self.parts = {}  # name -> list of corners (p, n, colour, emissive, uv, roughness, metallic)

    def add(self, name, tris, colour, roughness, metallic=0.0, uv=None):
        """uv: optional function of a model-space point -> (u, v) (decal parts)."""
        corners = self.parts.setdefault(name, [])
        for ps, ns in tris:
            for p, n in zip(ps, ns):
                p = tuple(float(c) for c in p)
                corners.append((p, tuple(float(c) for c in n), tuple(colour), 0.0, uv(p) if uv else (0.0, 0.0),
                                roughness, metallic))

    def add_corners(self, name, corners):
        self.parts.setdefault(name, []).extend(corners)


def model_space(parts):
    """The legacy (name, matrix, pivot, verts, idx) list in model space, per triangle corner."""
    out = {}
    for name, matrix, pivot, verts, idx in parts:
        m = np.array(matrix, dtype=np.float64).reshape(4, 4).T
        v = np.array(verts, dtype=np.float64).reshape(-1, 14)
        if len(v) == 0:
            out[name] = []
            continue
        pos = v[:, 0:3] @ m[:3, :3].T + m[:3, 3]
        nrm = v[:, 3:6] @ np.linalg.inv(m[:3, :3])
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-20)
        corners = []
        for i in idx:
            corners.append((tuple(pos[i]), tuple(nrm[i]), tuple(v[i, 6:9]), float(v[i, 9]), tuple(v[i, 10:12]),
                            float(v[i, 12]), float(v[i, 13])))
        out[name] = corners
    return out


def triangles(corners):
    return [corners[i:i + 3] for i in range(0, len(corners), 3)]


def split_body_and_legend(corners):
    """The cap body is the colour of its walls; anything else on the part is legend."""
    area = {}
    for tri in triangles(corners):
        p = np.array([c[0] for c in tri])
        n = np.cross(p[1] - p[0], p[2] - p[0])
        a = np.linalg.norm(n) / 2
        if a > 0 and abs(n[2]) / (2 * a) < 0.5:  # a wall
            area[tri[0][2]] = area.get(tri[0][2], 0.0) + a
    body_colour = max(area, key=area.get)
    body = [t for t in triangles(corners) if t[0][2] == body_colour]
    legend = [t for t in triangles(corners) if t[0][2] != body_colour]
    return body, legend, body_colour


SPEAKER_PARTS = ("speaker_panel", "speaker_grille", "mic_port")


def fit_speaker(corners, old, name):
    """The carried speaker sits ~5 mm toward the screen of TE's drawing: fit the
    panel's plan rectangle to spec.SPEAKER (grille with it) and put the mic hole
    where the drawing has it."""
    pts = np.array([c[0] for c in old["speaker_panel"]])
    lo, hi = pts[:, :2].min(axis=0), pts[:, :2].max(axis=0)
    sx0, sy0, sx1, sy1 = spec.SPEAKER
    t_lo = np.array([X(sx0), Y(sy1)])
    t_hi = np.array([X(sx1), Y(sy0)])
    scale = (t_hi - t_lo) / (hi - lo)
    shift = np.zeros(2)
    if name == "mic_port":
        m = np.array([c[0] for c in corners])[:, :2]
        centre = (m.min(axis=0) + m.max(axis=0)) / 2
        shift = np.array([X(spec.MIC[0]), Y(spec.MIC[1])]) - (t_lo + (centre - lo) * scale)
    out = []
    for p, n, *rest in corners:
        q = t_lo + (np.array(p[:2]) - lo) * scale + shift
        out.append(((float(q[0]), float(q[1]), p[2]), n, *rest))
    return out


def carry(corners, mode, scale, y_remap=None):
    base = {"upper": UPPER, "deck": DECK}.get(mode)
    out = []
    for p, n, *rest in corners:
        x, y, z = p
        if base is not None:
            z = base + (z - legacy.TOP_FACE) * scale
        if y_remap is not None:
            y = y_remap(y)
        out.append(((x, y, z), n, *rest))
    return out


def decal_tiles():
    """Every decal part's footprint (reference mm) and include inset (artwork/atlas.py)."""
    cell = spec.CELL.mm
    tiles = []
    for name, col, row in spec.FULL_KEYS:
        x, y = spec.COLUMN_LEFT[col], spec.ROW_TOP[row]
        tiles.append(keys_atlas.Tile(name, (x, y, x + cell, y + cell), (1.0, 1.0)))
    cw, ch = spec.SHORT_CAP
    pw, ph = spec.PLATE
    for cap, plate, col, row in spec.FUNCTION_CELLS:
        x, y = spec.COLUMN_LEFT[col], spec.ROW_TOP[row]
        tiles.append(keys_atlas.Tile(cap, (x + (cell - cw) / 2, y + 0.5, x + (cell + cw) / 2, y + 0.5 + ch), (0.6, 0.6)))
        tiles.append(keys_atlas.Tile(plate, (x + (cell - pw) / 2, y + cell - ph, x + (cell + pw) / 2, y + cell), (1.0, 0.7)))
    sx0, sx1 = spec.SMALL_KEY_X
    for name, (y0, y1) in spec.SMALL_KEY_Y.items():
        tiles.append(keys_atlas.Tile(name, (sx0, y0, sx1, y1), (1.0, 1.0)))  # past the drawn key border
    for name, rect in spec.PORT_TABS.items():
        tiles.append(keys_atlas.Tile(name, rect, (0.35, 0.35)))
    return tiles


def build_face(fills):
    """The deck silkscreen: everything inked on the deck that is not a key, plate,
    small key or knob (those parts carry their own artwork)."""
    cell, m = spec.CELL.mm, 0.3
    rects = []
    for _, col, row in spec.FULL_KEYS:
        x, y = spec.COLUMN_LEFT[col], spec.ROW_TOP[row]
        rects.append((x - m, y - m, x + cell + m, y + cell + m))
    for _, _, col, row in spec.FUNCTION_CELLS:
        x, y = spec.COLUMN_LEFT[col], spec.ROW_TOP[row]
        rects.append((x - m, y - m, x + cell + m, y + cell + m))
    sx0, sx1 = spec.SMALL_KEY_X
    for y0, y1 in spec.SMALL_KEY_Y.values():
        rects.append((sx0 - 0.8, y0 - 0.8, sx1 + 0.8, y1 + 0.8))   # drawn borders run wider than the keys
    # Above the step: nothing but the label plate (ports, speaker and screen are parts).
    lx0, ly0, lx1, ly1 = LABEL_PLATE
    rects.append((lx1 + 0.05, ly0 - 0.4, spec.WIDTH.mm, spec.STEP_Y.mm + 1.0))
    rects.append((0.0, ly1 - 0.05, lx1 + 0.05, spec.STEP_Y.mm + 1.0))
    for tx0, ty0, tx1, ty1 in spec.PORT_TABS.values():   # inserts carry their own lettering
        rects.append((tx0 - 0.3, ty0 - 0.6, tx1 + 0.3, ty1 + 0.3))
    px0, py0, px1, py1 = spec.POWER_SLIDER
    rects.append((px0 - 0.5, py0, px1 + 0.5, py1 + 0.5))
    rects.append(FADER_SLOT)
    discs = [(x, y, spec.KNOB_DISC[0] / 2 + m) for x, y in spec.KNOBS.values()]
    return face_atlas.build(fills, FACE_REGION, rects, discs)


def build_atlas():
    """-> (tiles by part name, atlas height, level-0 atlas, preview)."""
    fills, _ = svg_import.load()
    tiles = decal_tiles()
    level0, preview = keys_atlas.build(fills, tiles)
    return {t.name: t for t in tiles}, level0.shape[0], level0, preview, fills


def build(usdz, tiles, atlas_height):
    old = model_space(legacy.extract_parts(usdz))
    b = Builder()

    def decal_uv(name):
        tile = tiles[name]
        # model metres -> reference mm (the inverse of X and Y) -> atlas UV
        return lambda p: keys_atlas.uv(tile, atlas_height, p[0] * 1000.0 + 88.0, 120.0 - p[1] * 1000.0)
    cream, green, gap = legacy.CREAM, legacy.GREEN, legacy.GAP

    # Chassis: the dark-green shell, then the cream body in two steps: the
    # upper section (ports, label plate, speaker, screen) and the lower deck.
    x0, x1 = X(0.0), X(spec.WIDTH.mm)
    r = spec.CORNER_RADIUS.mm / 1000
    groove = spec.SHELL_GROOVE.mm / 1000
    b.add("chassis_base", slab(x0, x1, Y(spec.DEPTH.mm), Y(0.0), Z(0.0), Z(spec.SHELL_TOP.mm), r, groove, 0.0005), green, 0.45)
    # The upper section: full height behind the screen; under the screen it
    # stops just below the icon plane, and the acrylic closes it (D5).
    wy0, wy1 = spec.WINDOW[1], spec.WINDOW[3]
    floor_z = spec.LCD_Z.mm - 0.25       # the chassis under the screen
    well_z = spec.LCD_Z.mm - 0.15        # the backlit floor round the LCD
    upper = slab(x0, x1, Y(wy0), Y(0.0), Z(spec.SHELL_TOP.mm), UPPER, r, spec.TOP_FILLET.mm / 1000, groove)
    upper += slab(x0, x1, Y(spec.STEP_Y.mm), Y(wy0 - 0.5), Z(spec.SHELL_TOP.mm), Z(floor_z), r, 0.0, groove)
    inset = 0.00002  # the deck's walls just inside the upper section's where they overlap
    lower = slab(x0 + inset, x1 - inset, Y(spec.DEPTH.mm), Y(spec.STEP_Y.mm - 1.0), Z(spec.SHELL_TOP.mm), DECK, r,
                 spec.TOP_FILLET.mm / 1000, groove)
    for ps, ns in upper + lower:
        target = "chassis_topPlate" if np.mean([n[2] for n in ns]) > 0.5 else "chassis_edgeBand"
        b.add(target, [(ps, ns)], cream, 0.62)

    cell = spec.CELL.mm
    def cell_centre(col, row):
        return X(spec.COLUMN_LEFT[col] + cell / 2), Y(spec.ROW_TOP[row] + cell / 2)

    key_h = spec.KEY_HEIGHT.mm / 1000
    taper = (cell - spec.KEY_TOP.mm) / 1000
    fillet = spec.KEY_FILLET.mm / 1000
    corner = spec.KEY_CORNER.mm / 1000
    g = spec.GAP.mm / 1000

    def rebuild_key(name, cx, cy, w, h, height, well=True, well_size=None):
        body, _legend, colour = split_body_and_legend(old[name])
        roughness = min(body[0][0][5], CAP_ROUGHNESS)
        top_w, top_h = w - taper, h - taper
        b.add(name, keycap(cx, cy, w, h, DECK, DECK + height, top_w, top_h, corner, fillet), colour, roughness,
              uv=decal_uv(name))
        if well:
            ww, wh = well_size or (w, h)
            b.add(name + "_well", frame(cx, cy, ww, wh, g, DECK + 0.00002, corner), gap, 0.7)

    for name, col, row in spec.FULL_KEYS:
        cx, cy = cell_centre(col, row)
        rebuild_key(name, cx, cy, cell / 1000, cell / 1000, key_h)

    for cap, plate, col, row in spec.FUNCTION_CELLS:
        cx, cy = cell_centre(col, row)
        top = spec.ROW_TOP[row]
        cw, ch = spec.SHORT_CAP
        rebuild_key(cap, cx, Y(top + 0.5 + ch / 2), cw / 1000, ch / 1000, key_h, well=False)
        pw, ph = spec.PLATE
        body, _legend, colour = split_body_and_legend(old[plate])
        pz = DECK + spec.PLATE_HEIGHT.mm / 1000
        pcy = Y(top + cell - ph / 2)
        b.add(plate, keycap(cx, pcy, pw / 1000, ph / 1000, DECK, pz, pw / 1000 - 0.0002, ph / 1000 - 0.0002,
                            corner, 0.00015, fillet_steps=3), colour, body[0][0][5], uv=decal_uv(plate))
        b.add(cap + "_well", frame(cx, cy, cell / 1000, cell / 1000, g, DECK + 0.00002, corner), gap, 0.7)

    sx0, sx1 = spec.SMALL_KEY_X
    for name, (y0, y1) in spec.SMALL_KEY_Y.items():
        w, h = (sx1 - sx0) / 1000, (y1 - y0) / 1000
        rebuild_key(name, X((sx0 + sx1) / 2), Y((y0 + y1) / 2), w, h, spec.SMALL_KEY_HEIGHT.mm / 1000)

    # Knobs: a coloured disc, a frosted stem, a flat coloured cap; X has a dark collar.
    stem_colour = legacy.srgb("#EDEBE3")
    knob_colours = {"knob_volume": legacy.KEY_WHITE, "knob_x": legacy.ORANGE, "knob_y": legacy.GREEN}
    disc_d, disc_h = spec.KNOB_DISC
    stem_d, stem_from, stem_to = spec.KNOB_STEM
    cap_d, cap_top = spec.KNOB_CAP
    for name, (kx, ky) in spec.KNOBS.items():
        cx, cy = X(kx), Y(ky)
        colour = knob_colours[name]
        b.add(name, cylinder(cx, cy, DECK, DECK + disc_h / 1000, disc_d / 2000, 0.0006), colour, 0.45)
        b.add(name, cylinder(cx, cy, DECK + stem_from / 1000, DECK + stem_to / 1000, stem_d / 2000, 0.0002), stem_colour, 0.3)
        b.add(name, cylinder(cx, cy, DECK + stem_to / 1000, DECK + cap_top / 1000, cap_d / 2000, 0.0008), colour, 0.42)
        if name == "knob_x":
            b.add(name, cylinder(cx, cy, DECK + disc_h / 1000, DECK + disc_h / 1000 + 0.0008, stem_d / 2000 + 0.0006, 0.0002),
                  gap, 0.6)

    # The upper section's inserts, from the front drawing: the label plate and
    # the port strip's coloured inserts stand 0.15 mm proud; their lettering is
    # printed (face texture and keys atlas).
    proud = UPPER_TOP_MM + spec.INSERT_PROUD.mm
    b.add("brand_panel", ref_slab(spec.LABEL_PLATE, UPPER_TOP_MM - 0.2, proud, 0.6, 0.1), legacy.LABEL, 0.40)
    tab_colours = {"port_output_tab": legacy.LABEL, "port_input_tab": legacy.ORANGE, "port_sync_tab": legacy.GREEN,
                   "port_midi_tab": legacy.GREEN, "port_usb_tab": legacy.KEY_BEIGE}
    for name, rect in spec.PORT_TABS.items():
        b.add(name, ref_slab(rect, UPPER_TOP_MM - 0.2, proud, 0.4, 0.08), tab_colours[name], 0.45, uv=decal_uv(name))

    # The screen (D5): the icon plane at z 14.4 (the LCD's own image), a
    # backlit floor of the same green round it, a dark carrier ring under the
    # acrylic's edge, and the 1 mm acrylic window on top (drawn last, blended).
    old_lcd = np.array([c[0] for c in old["display_surface"]])
    lcd = (old_lcd[:, 0].min() * 1000 + 88, 120 - old_lcd[:, 1].max() * 1000,
           old_lcd[:, 0].max() * 1000 + 88, 120 - old_lcd[:, 1].min() * 1000)
    lcd = (lcd[0], max(lcd[1], wy0 + spec.BEZEL_WIDTH.mm + 0.4), lcd[2], min(lcd[3], wy1 - spec.BEZEL_WIDTH.mm - 0.4))
    # UVs as the old asset had them: u along +x, v = 0 at the step side.
    lcd_uv = lambda p, lcd=lcd: ((p[0] * 1000 + 88 - lcd[0]) / (lcd[2] - lcd[0]), (lcd[3] - (120 - p[1] * 1000)) / (lcd[3] - lcd[1]))
    b.add("display_surface", ref_quad(lcd, spec.LCD_Z.mm), legacy.LABEL, 0.4, uv=lcd_uv)
    # The floor samples the LCD image's own border (its plain backlit green).
    b.add("display_well", ref_quad(spec.WINDOW, well_z), legacy.LABEL, 0.4, uv=lambda p: (0.002, 0.5))
    bw = spec.BEZEL_WIDTH.mm
    wx0, _, wx1, _ = spec.WINDOW
    for ring in ((wx0, wy0, wx1, wy0 + bw), (wx0, wy1 - bw, wx1, wy1), (wx0, wy0, wx0 + bw, wy1), (wx1 - bw, wy0, wx1, wy1)):
        b.add("display_bezel", ref_slab(ring, floor_z, spec.GLASS_Z[0] - 0.05, 0.05), legacy.FRAME, 0.35)
    glass = legacy.srgb("#1E6B4F")
    b.add("display_glass", ref_slab(spec.WINDOW, spec.GLASS_Z[0], spec.GLASS_Z[1], r * 1000, 0.15), glass, 0.05)

    # Everything else is carried over, row by row.
    rebuilt = set(b.parts) | {n for n in old if n.endswith("_well")} | {"halo_ring", "port_power_tab"}
    screen_front, screen_back = Y(spec.SCREEN_FRONT_Y), None
    for name, corners in old.items():
        if name in rebuilt:
            continue
        if name.startswith(legacy.LAMP_PREFIX):
            b.add_corners(name, carry(corners, "deck", 1.0))
            continue
        if name not in spec.CARRY_OVER:
            raise SystemExit(f"{name}: not rebuilt and not in spec.CARRY_OVER")
        mode, scale = spec.CARRY_OVER[name]
        remap = None
        if name in SPEAKER_PARTS:
            corners = fit_speaker(corners, old, name)
        b.add_corners(name, carry(corners, mode, scale, remap))

    identity = [1.0 if r == c else 0.0 for c in range(4) for r in range(4)]
    parts = [("halo_ring", identity, [0.0, 0.0, 0.0], [], [])]
    # Glass last: the renderer draws everything before it opaque, then it blended.
    names = [n for n in b.parts if n != "display_glass"] + ["display_glass"]
    for name in names:
        parts.append(legacy.pack_part(name, IdentityMatrix(), b.parts[name]))
    return parts


class IdentityMatrix:
    """What pack_part reads from a Gf.Matrix4d: m[r][c]."""
    def __getitem__(self, r):
        return [1.0 if r == c else 0.0 for c in range(4)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("usdz")
    ap.add_argument("--out", default="assets/stage/EP40.hmesh")
    ap.add_argument("--atlas", default="assets/stage/ep40_keys_sdf.htex")
    ap.add_argument("--face", default="assets/stage/ep40_face_sdf.htex")
    ap.add_argument("--preview", default="build/model/keys_atlas.png", help="review image of the atlas ('' for none)")
    a = ap.parse_args()
    tiles, atlas_height, level0, preview, fills = build_atlas()
    for t in tiles.values():
        if sum(t.ink_px.values()) == 0:
            raise SystemExit(f"{t.name}: no legend ink inside its include rectangle")
    parts = build(a.usdz, tiles, atlas_height)
    data = legacy.to_v3(parts, Path(a.usdz).name, decal_parts=set(tiles))
    Path(a.out).write_bytes(data)
    import htex_io
    from artwork import sdf
    Path(a.atlas).write_bytes(htex_io.write_htex(sdf.mips(level0), 3, channels=b"GOWC", flags=2))
    print(f"wrote {a.atlas}: {level0.shape[1]} x {level0.shape[0]}, {len(tiles)} tiles")
    face0, face_preview, face_ink = build_face(fills)
    if min(face_ink) == 0:
        raise SystemExit("deck silkscreen: an ink is empty")
    Path(a.face).write_bytes(htex_io.write_htex(sdf.mips(face0), 2, channels=b"GO__", flags=2))
    print(f"wrote {a.face}: {face0.shape[1]} x {face0.shape[0]}, region {FACE_REGION} mm")
    if a.preview:
        from PIL import Image
        Path(a.preview).parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(preview).save(a.preview)
        Image.fromarray(face_preview).save(Path(a.preview).with_name("face_atlas.png"))
    tris = sum(len(p[4]) for p in parts) // 3
    print(f"wrote {a.out}: {len(parts)} parts, {tris} triangles, {len(data)} bytes")


if __name__ == "__main__":
    main()
