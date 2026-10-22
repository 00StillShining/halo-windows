"""Convert halo's Blender-authored EP40.usdz into a compact .hmesh file for the
Windows app.

The macOS app loads the USDZ through RealityKit. On Windows, we pre-bake the
same geometry once with Pixar's USD library (pip install usd-core) into a
simple binary the C++ renderer can read with no dependencies.

Each direct child of /ep40_root becomes one *part*, named exactly like the prim
(pad_7, group_a, button_play, display_surface, ...). This keeps halo's frozen
entity-name contract (DD-006), so the app can move pads and light LEDs by name.

On the way through, the bake refines what RealityKit got for free or never had:

  * Smooth, area-weighted normals. Blender exported every bevel flat-shaded, so
    edges rendered as faceted strips. Weighting by face area keeps big faces
    perfectly flat while each bevel rolls the light over the edge, which is what
    makes a moulded part read as real.
  * Physically based material values (base colour, roughness, metallic) per
    vertex: the plan's measured palette (docs/redesign/riddim-3d-plan.md 2.5).
  * Rounded-corner chassis slabs in place of the bevelled cubes, with the
    dark-green 3.8 mm bottom shell.
  * The 16 + 2 LEDs as flush, unlit 1.8 mm discs at TE's positions.
  * The floating legend_25..29 meshes dropped (FacePrint.h prints that text)
    and halo_ring kept only as a zero-triangle anchor (the ring is a floor pool).

Format (little endian):
    char[4]  "HMSH"
    u32      version (2; version 1 had no roughness/metallic)
    u32      partCount
    per part:
        u16      nameLength, then UTF-8 name bytes
        f32[16]  part -> model transform, column-major
        f32[3]   pivot (part-local bounds centre)
        u32      vertexCount
        f32[14]  per vertex: position xyz, normal xyz, colour rgb, emissive, uv,
                 roughness, metallic
        u32      indexCount
        u32[]    indices

Model space is the Blender one: Z-up, metres, X = width, Y = depth (far = +Y).

Usage: python -I tools/model/usdz_to_hmesh.py <EP40.usdz> assets/stage/EP40.hmesh [--format v3|v2]
(v3, the default, is described in tools/model/hmesh_io.py; v2 below.)
"""

import math
import struct
import sys
import tempfile
import zipfile
from collections import defaultdict
from pathlib import Path

from pxr import Gf, Usd, UsdGeom, UsdShade

FORMAT_VERSION = 2

# Device constants from tools/blender/build_ep40.py (portrait, metres).
W, D, TH = 0.176, 0.240, 0.016
TOP = TH / 2.0
TOP_FACE = TOP + 0.0014

# Faces whose normals differ by more than this stay hard-edged. Blender's bevels
# use three segments (30 degrees apart), so anything below 90 keeps box corners
# that were never bevelled crisp while every bevel shades smoothly.
SMOOTH_ANGLE = math.radians(52.0)

def srgb(hex_colour):
    """sRGB hex -> linear RGB, with the exact piecewise curve."""
    h = hex_colour.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return tuple(out)


# The EP-40's plastics and inks (docs/redesign/riddim-3d-plan.md section 2.5),
# albedo in sRGB hex, stored linear. Metallic is 0 everywhere: nothing on the
# device is metal. These are facts of the hardware, not look tokens.
CREAM = srgb("#DCD2BF")        # chassis, deck, port strip (PA66 satin)
LABEL = srgb("#ECEBE6")        # label plate and the white printed plates
KEY_WHITE = srgb("#E9E9E5")    # numeral pads, - and +
KEY_BEIGE = srgb("#DDD3C2")    # group pads and the beige function caps
GREEN = srgb("#0A4A36")        # bottom shell, PLAY, LOOP, CORRECT, TIMING, Y, SYNC tab
ORANGE = srgb("#FA5A16")       # INPUT, COMMIT, SAMPLE/CHOP, RECORD, X, feet
INK_GREEN = srgb("#00503A")    # engraved brand ink
INK_ORANGE = srgb("#FF681D")   # SUPERTONE, ENTER, GAIN
INK_WHITE = srgb("#F4F2EC")    # RECORD / PLAY legends
INK_CHARCOAL = srgb("#2B2B28")  # ORIGINAL LAYERING MACHINE
GAP = srgb("#1C1B19")          # apertures and liners
FRAME = srgb("#0B2E24")        # glossy screen frame
LED_OFF = srgb("#3E3A35")      # unlit LEDs (the asset never bakes one lit)

# Per Blender material. Anything not listed keeps the USD values.
MATERIALS = {
    "m_body":    dict(colour=CREAM, roughness=0.62),
    "m_plate":   dict(colour=CREAM, roughness=0.62),
    "m_edge":    dict(colour=GREEN, roughness=0.45, metallic=0.0),
    "m_cap":     dict(colour=KEY_WHITE, roughness=0.45),
    "m_brand":   dict(colour=LABEL, roughness=0.40),
    "m_brandG":  dict(colour=INK_GREEN, roughness=0.48),
    "m_brandI":  dict(colour=INK_CHARCOAL, roughness=0.48),
    "m_brandO":  dict(colour=INK_ORANGE, roughness=0.48),
    "m_dark":    dict(colour=GAP, roughness=0.70),
    "m_ink":     dict(colour=LED_OFF, roughness=0.35),
    "m_green":   dict(colour=GREEN, roughness=0.45),
    "m_greenC":  dict(colour=GREEN, roughness=0.42),
    "m_greenD":  dict(colour=GREEN, roughness=0.45),
    "m_orange":  dict(colour=ORANGE, roughness=0.45),
    "m_orangeC": dict(colour=ORANGE, roughness=0.42),
    "m_legendG": dict(colour=INK_GREEN, roughness=0.53),
    "m_legendL": dict(colour=INK_WHITE, roughness=0.53),
    "m_legendO": dict(colour=INK_ORANGE, roughness=0.53),
}

# Where one Blender material stands for different plastics on different parts.
BEIGE_CAPS = ("group_a", "group_b", "group_c", "group_d", "button_sound", "button_main", "button_tempo",
              "button_fx", "button_erase", "button_keys", "button_shift", "fader_mode_button")
PART_MATERIALS = {
    **{(p, "m_cap"): dict(colour=KEY_BEIGE, roughness=0.45) for p in BEIGE_CAPS},
    ("display_bezel", "m_dark"): dict(colour=FRAME, roughness=0.15),
    ("knob_volume", "m_edge"): dict(colour=KEY_WHITE, roughness=0.45),  # VOLUME is white
    ("ports_strip", "m_edge"): dict(colour=CREAM, roughness=0.62),
    ("speaker_panel", "m_brand"): dict(colour=CREAM, roughness=0.62),
    ("mic_port", "m_ink"): dict(colour=GAP, roughness=0.70),
    ("halo_ring", "m_ink"): dict(colour=GAP, roughness=0.70),
}

# Status lamps the Blender model baked "lit" (orange). An emissive may only show
# observed state (plan D4), so every lamp ships unlit.
LAMP_PREFIX = "status_lamp_"
# legend_25..29 floated 1.65 mm above the plate, and legend_26 read "DUB" where
# the device says "BPM". Their text is printed by Source/Stage/FacePrint.h.
DROPPED_PARTS = {"legend_25", "legend_26", "legend_27", "legend_28", "legend_29"}
# The ring is a pool of light on the floor now (plan D6); the name stays as a
# zero-triangle anchor so the DD-006 contract still resolves.
ANCHOR_PARTS = {"halo_ring"}


def material_props(prim, part_name=""):
    """(colour, emissive, roughness, metallic) from the bound UsdPreviewSurface."""
    material = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()[0]
    colour, emissive, roughness, metallic = (0.8, 0.8, 0.8), 0.0, 0.5, 0.0
    if not material:
        return colour, emissive, roughness, metallic
    for child in Usd.PrimRange(material.GetPrim()):
        shader = UsdShade.Shader(child)
        if not shader or shader.GetIdAttr().Get() != "UsdPreviewSurface":
            continue
        diffuse = shader.GetInput("diffuseColor")
        if diffuse and diffuse.Get() is not None:
            colour = tuple(diffuse.Get())
        emit = shader.GetInput("emissiveColor")
        if emit and emit.Get() is not None:
            emissive = max(emit.Get())
        rough = shader.GetInput("roughness")
        if rough and rough.Get() is not None:
            roughness = float(rough.Get())
        metal = shader.GetInput("metallic")
        if metal and metal.Get() is not None:
            metallic = float(metal.Get())
        break
    name = material.GetPrim().GetName()
    override = dict(MATERIALS.get(name, {}))
    override.update(PART_MATERIALS.get((part_name, name), {}))
    if part_name.startswith(LAMP_PREFIX):
        override = dict(colour=LED_OFF, roughness=0.35)
        emissive = 0.0
    return (override.get("colour", colour), emissive,
            override.get("roughness", roughness), override.get("metallic", metallic))


def mesh_triangles(mesh, to_part):
    """Yield ((p0, p1, p2), (uv0, uv1, uv2)) per triangle, in part space."""
    points = mesh.GetPointsAttr().Get() or []
    counts = mesh.GetFaceVertexCountsAttr().Get() or []
    indices = mesh.GetFaceVertexIndicesAttr().Get() or []

    st = UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st")
    uvs, uv_interp = None, None
    if st and st.HasValue():
        uvs = st.ComputeFlattened()
        uv_interp = st.GetInterpolation()

    def uv_of(c):
        if uvs is not None and uv_interp == "faceVarying":
            return tuple(uvs[c])
        if uvs is not None and uv_interp == "vertex":
            return tuple(uvs[indices[c]])
        return (0.0, 0.0)

    corner = 0
    for count in counts:
        face = list(range(corner, corner + count))
        for k in range(1, count - 1):  # fan triangulation (faces are convex)
            tri = (face[0], face[k], face[k + 1])
            ps = tuple(to_part.Transform(Gf.Vec3d(points[indices[c]])) for c in tri)
            yield ps, tuple(uv_of(c) for c in tri)
        corner += count


def smooth_normals(triangles):
    """Area-weighted vertex normals within SMOOTH_ANGLE, per triangle corner.

    `triangles` is a list of (p0, p1, p2). Returns a parallel list of three
    normals each. Large faces dominate their own corners, so they stay flat;
    narrow bevel strips blend between their neighbours.
    """
    face_n, face_a = [], []
    for p0, p1, p2 in triangles:
        c = Gf.Cross(p1 - p0, p2 - p0)
        length = c.GetLength()
        face_a.append(length * 0.5)
        face_n.append(c / length if length > 0 else Gf.Vec3d(0, 0, 1))

    key = lambda p: (round(p[0], 7), round(p[1], 7), round(p[2], 7))
    incident = defaultdict(list)
    for t, tri in enumerate(triangles):
        for p in tri:
            incident[key(p)].append(t)

    cos_limit = math.cos(SMOOTH_ANGLE)
    result = []
    for t, tri in enumerate(triangles):
        n_t = face_n[t]
        corners = []
        for p in tri:
            acc = Gf.Vec3d(0, 0, 0)
            for u in incident[key(p)]:
                if Gf.Dot(face_n[u], n_t) >= cos_limit:
                    acc += face_n[u] * face_a[u]
            corners.append(acc.GetNormalized() if acc.GetLength() > 0 else n_t)
        result.append(corners)
    return result


# ------------------------------------------------------------------ procedural
def rounded_rect(hx, hy, r, steps=10):
    """Counter-clockwise outline of a rounded rectangle, with outward normals."""
    out = []
    centres = ((hx - r, hy - r, 0.0), (-hx + r, hy - r, 90.0),
               (-hx + r, -hy + r, 180.0), (hx - r, -hy + r, 270.0))
    for cx, cy, start in centres:
        for i in range(steps + 1):
            a = math.radians(start + 90.0 * i / steps)
            out.append(((cx, cy), (math.cos(a), math.sin(a))))
    return out


def rounded_slab(hx, hy, corner, z0, z1, top_fillet, bottom_fillet, cx=0.0, cy=0.0):
    """Triangles (with analytic normals) of a slab with rounded corners in plan
    and filleted top/bottom edges, the way an injection-moulded case reads."""
    profile = []  # (inset, z, normal_out, normal_z), bottom to top
    for i in range(7):
        a = math.radians(-90.0 + 90.0 * i / 6)
        profile.append((bottom_fillet * (1 - math.cos(a)),
                        z0 + bottom_fillet * (1 + math.sin(a)), math.cos(a), math.sin(a)))
    for i in range(7):
        a = math.radians(90.0 * i / 6)
        profile.append((top_fillet * (1 - math.cos(a)),
                        z1 - top_fillet * (1 - math.sin(a)), math.cos(a), math.sin(a)))

    outline = rounded_rect(hx, hy, corner)

    def ring(inset, z):
        pts = []
        for (ox, oy), (nx, ny) in outline:
            r = corner - inset
            pts.append(Gf.Vec3d(cx + ox + nx * r, cy + oy + ny * r, z))
        return pts

    rings = [ring(inset, z) for inset, z, _, _ in profile]
    tris = []  # (positions, normals)
    n = len(outline)
    for k in range(len(profile) - 1):
        _, _, no0, nz0 = profile[k]
        _, _, no1, nz1 = profile[k + 1]
        for i in range(n):
            j = (i + 1) % n
            (_, (ax, ay)), (_, (bx, by)) = outline[i], outline[j]
            na0 = Gf.Vec3d(ax * no0, ay * no0, nz0)
            nb0 = Gf.Vec3d(bx * no0, by * no0, nz0)
            na1 = Gf.Vec3d(ax * no1, ay * no1, nz1)
            nb1 = Gf.Vec3d(bx * no1, by * no1, nz1)
            a0, b0, a1, b1 = rings[k][i], rings[k][j], rings[k + 1][i], rings[k + 1][j]
            tris.append(((a0, b0, b1), (na0, nb0, nb1)))
            tris.append(((a0, b1, a1), (na0, nb1, na1)))
    top, bottom = rings[-1], rings[0]
    up, down = Gf.Vec3d(0, 0, 1), Gf.Vec3d(0, 0, -1)
    for i in range(1, n - 1):
        tris.append(((top[0], top[i], top[i + 1]), (up, up, up)))
        tris.append(((bottom[0], bottom[i + 1], bottom[i]), (down, down, down)))
    return tris




# Parts whose bevelled-cube geometry is replaced by procedural rounded slabs.
# The real case has softly radiused corners in plan, not 2 mm chamfers.
CHASSIS = {
    "chassis_base":     lambda: rounded_slab(W / 2, D / 2, 0.0052, -TOP, TOP, 0.0016, 0.0012),
    "chassis_topPlate": lambda: rounded_slab(W / 2 - 0.001, D / 2 - 0.001, 0.0044,
                                             TOP - 0.0002, TOP + 0.0014, 0.00055, 0.0),
    # The dark-green bottom shell: a 3.8 mm stripe all round (plan section 2.2),
    # standing 0.2 mm proud of the cream walls so the two never z-fight.
    "chassis_edgeBand": lambda: rounded_slab(W / 2 + 0.0002, D / 2 + 0.0002, 0.0054,
                                             -TOP, -TOP + 0.0038, 0.0004, 0.0008),
}

# The 16 + 2 LEDs (plan section 2.3, reference mm: x from the left edge, y from
# the port edge). The Blender lamps were 2.5 mm pills floating 1.3 mm above the
# plate, about 4 mm right of the real positions, with the row above 7 8 9 left
# out; they are replaced by flush 1.8 mm discs at TE's positions, 0.02 mm clear
# of the plate (it has no holes until the compiler builds the deck in M5a).
LED_ROWS_Y = (132.0, 156.0, 179.9, 204.1)
LED_COLS_X = (35.9, 59.9, 84.0, 108.0)
LED_AUX = ((132.2, 204.1), (140.1, 204.1))
LED_DIAMETER = 0.0018
LED_HEIGHT = 0.00015
LAMP_CLEARANCE = 0.00002


def ref_to_model(x_mm, y_mm):
    """Reference drawing mm (x right, y down from the port edge) -> model metres."""
    return (x_mm - 88.0) / 1000.0, (120.0 - y_mm) / 1000.0


def led_parts():
    """(name, model-space triangles with normals) for every LED."""
    spots = [(f"status_lamp_{r}_{c}", x, y) for r, y in enumerate(LED_ROWS_Y) for c, x in enumerate(LED_COLS_X)]
    spots += [(f"status_lamp_aux_{i}", x, y) for i, (x, y) in enumerate(LED_AUX)]
    z0 = TOP_FACE + LAMP_CLEARANCE
    out = []
    for name, x, y in spots:
        mx, my = ref_to_model(x, y)
        out.append((name, disc(mx, my, z0, z0 + LED_HEIGHT, LED_DIAMETER / 2, 0.0001)))
    return out


def disc(cx, cy, z0, z1, radius, fillet, segments=32, fillet_steps=3):
    """A low disc (wall plus filleted top) with analytic normals, ~200 triangles."""
    rings = [(radius, z0, 1.0, 0.0)]  # (radius, z, normal_out, normal_z)
    for i in range(fillet_steps + 1):
        a = math.radians(90.0 * i / fillet_steps)
        rings.append((radius - fillet * (1 - math.cos(a)), z1 - fillet * (1 - math.sin(a)), math.cos(a), math.sin(a)))
    tris = []
    for k in range(len(rings) - 1):
        r0, za, no0, nz0 = rings[k]
        r1, zb, no1, nz1 = rings[k + 1]
        for i in range(segments):
            a0, a1 = 2 * math.pi * i / segments, 2 * math.pi * (i + 1) / segments
            c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
            p = lambda r, z, c, s: Gf.Vec3d(cx + r * c, cy + r * s, z)
            n = lambda no, nz, c, s: Gf.Vec3d(no * c, no * s, nz)
            A, B = p(r0, za, c0, s0), p(r0, za, c1, s1)
            C, D_ = p(r1, zb, c1, s1), p(r1, zb, c0, s0)
            nA, nB, nC, nD = n(no0, nz0, c0, s0), n(no0, nz0, c1, s1), n(no1, nz1, c1, s1), n(no1, nz1, c0, s0)
            tris.append(((A, B, C), (nA, nB, nC)))
            tris.append(((A, C, D_), (nA, nC, nD)))
    r_top, z_top = rings[-1][0], rings[-1][1]
    up = Gf.Vec3d(0, 0, 1)
    centre = Gf.Vec3d(cx, cy, z_top)
    for i in range(segments):
        a0, a1 = 2 * math.pi * i / segments, 2 * math.pi * (i + 1) / segments
        tris.append(((centre, Gf.Vec3d(cx + r_top * math.cos(a0), cy + r_top * math.sin(a0), z_top),
                      Gf.Vec3d(cx + r_top * math.cos(a1), cy + r_top * math.sin(a1), z_top)), (up, up, up)))
    return tris

# ------------------------------------------------------------------ conversion
def convert(usdz_path, out_path, fmt="v3"):
    parts = extract_parts(usdz_path)
    if fmt == "v2":
        write_v2(parts, out_path)
    else:
        data = to_v3(parts, Path(usdz_path).name)
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        Path(out_path).write_bytes(data)
        print(f"wrote {out_path}: v3, {len(parts)} parts, {len(data)} bytes")


def extract_parts(usdz_path):
    """The Blender model as a list of (name, part-to-model column-major matrix, pivot,
    vertices of 14 floats, indices), with this script's materials and fixes applied."""
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(usdz_path) as archive:
            archive.extractall(tmp)
        usdc = next(Path(tmp).glob("*.usd*"))
        stage = Usd.Stage.Open(str(usdc))
        root = stage.GetPrimAtPath("/ep40_root")
        cache = UsdGeom.XformCache()
        root_inv = cache.GetLocalToWorldTransform(root).GetInverse()

        parts = []
        for part in root.GetChildren():
            name = part.GetName()
            if name in DROPPED_PARTS:
                continue
            # USD uses row vectors: model = partLocal * part_to_model.
            part_to_model = cache.GetLocalToWorldTransform(part) * root_inv
            model_to_part = part_to_model.GetInverse()
            if name in ANCHOR_PARTS:
                parts.append((name, [part_to_model.GetTranspose()[r][c] for c in range(4) for r in range(4)],
                              [0.0, 0.0, 0.0], [], []))
                continue
            corners = []  # (position, normal, colour, emissive, uv, roughness, metallic)
            for prim in Usd.PrimRange(part):
                if not prim.IsA(UsdGeom.Mesh):
                    continue
                if UsdGeom.Imageable(prim).ComputeVisibility() == UsdGeom.Tokens.invisible:
                    continue
                mesh_to_part = (cache.GetLocalToWorldTransform(prim) * root_inv) * model_to_part
                colour, emissive, roughness, metallic = material_props(prim, name)

                if part.GetName() in CHASSIS:
                    for ps, ns in CHASSIS[part.GetName()]():
                        for p, n in zip(ps, ns):
                            corners.append((model_to_part.Transform(p),
                                            model_to_part.TransformDir(n).GetNormalized(),
                                            colour, emissive, (0.0, 0.0), roughness, metallic))
                    break

                tris = list(mesh_triangles(UsdGeom.Mesh(prim), mesh_to_part))
                normals = smooth_normals([ps for ps, _ in tris])
                for (ps, uvs), ns in zip(tris, normals):
                    for p, n, uv in zip(ps, ns, uvs):
                        corners.append((p, n, colour, emissive, uv, roughness, metallic))

            if corners and not name.startswith(LAMP_PREFIX):  # lamps: see led_parts()
                m = part_to_model.GetTranspose()
                parts.append(pack_part(name, m, corners))

        for name, tris in led_parts():
            corners = [(p, n, LED_OFF, 0.0, (0.0, 0.0), 0.35, 0.0) for ps, ns in tris for p, n in zip(ps, ns)]
            parts.append(pack_part(name, Gf.Matrix4d(1.0), corners))
    return parts


def write_v2(parts, out_path):
    with open(out_path, "wb") as out:
        out.write(b"HMSH")
        out.write(struct.pack("<II", FORMAT_VERSION, len(parts)))
        for name, matrix, pivot, verts, idx in parts:
            encoded = name.encode("utf-8")
            out.write(struct.pack("<H", len(encoded)))
            out.write(encoded)
            out.write(struct.pack("<16f", *matrix))
            out.write(struct.pack("<3f", *pivot))
            out.write(struct.pack("<I", len(verts)))
            for v in verts:
                out.write(struct.pack("<14f", *v))
            out.write(struct.pack("<I", len(idx)))
            out.write(struct.pack(f"<{len(idx)}I", *idx))

    total = sum(len(p[3]) for p in parts)
    print(f"wrote {out_path}: {len(parts)} parts, {total} vertices")


def pack_part(name, transposed, corners):
    """Dedupe identical corners into an indexed vertex list."""
    verts, idx, seen = [], [], {}
    lo, hi = [1e9] * 3, [-1e9] * 3
    for p, n, colour, emissive, uv, roughness, metallic in corners:
        v = (*p, *n, *colour, emissive, *uv, roughness, metallic)
        key = tuple(round(x, 6) for x in v)
        if key not in seen:
            seen[key] = len(verts)
            verts.append(v)
        idx.append(seen[key])
        lo = [min(a, b) for a, b in zip(lo, p)]
        hi = [max(a, b) for a, b in zip(hi, p)]
    # Gf matrices are row-major for row vectors; transposing that gives the
    # column-major, column-vector matrix the renderer expects.
    column_major = [transposed[r][c] for c in range(4) for r in range(4)]
    pivot = [(a + b) / 2 for a, b in zip(lo, hi)]
    return name, column_major, pivot, verts, idx


# ------------------------------------------------------------------ .hmesh v3
GRID_PARTS = ["pad_7", "pad_8", "pad_9", "pad_4", "pad_5", "pad_6",
              "pad_1", "pad_2", "pad_3", "pad_dot", "pad_0", "pad_enter"]   # Ep40MidiMapping.h order
GROUP_PARTS = ["group_a", "group_b", "group_c", "group_d"]
SLOTS = {name: i + 1 for i, name in enumerate(GRID_PARTS + GROUP_PARTS)}      # 0 = static
KEY_TRAVEL = 0.0026  # metres (plan O5 default; the stage's own spring decides the motion)

PALETTE_NAMES = {CREAM: "cream", LABEL: "label", KEY_WHITE: "key white", KEY_BEIGE: "key beige", GREEN: "green",
                 ORANGE: "orange", INK_GREEN: "ink green", INK_ORANGE: "ink orange", INK_WHITE: "ink white",
                 INK_CHARCOAL: "ink charcoal", GAP: "gap", FRAME: "frame", LED_OFF: "led off"}


def part_kind(name):
    import hmesh_io
    k = hmesh_io.KIND
    if name in GRID_PARTS:
        return k["pad"]
    if name in GROUP_PARTS:
        return k["group"]
    if name.startswith("button_") or name == "fader_mode_button":
        return k["button"]
    if name.startswith("knob_"):
        return k["knob"]
    if name.startswith("fader_"):
        return k["fader"]
    if name.startswith(LAMP_PREFIX):
        return k["led"]
    if name == "display_surface":
        return k["display"]
    if name in ANCHOR_PARTS:
        return k["anchor"]
    if name in ("chassis_topPlate", "brand_panel"):
        return k["plate"]
    return k["static"]


def led_for(name):
    """status_lamp_<row>_<col>: rows sit above 7 8 9 / 4 5 6 / 1 2 3 / . 0 ENTER; column 0
    is the group column (12 + row), columns 1-3 the pads (grid index). Aux lamps: none."""
    parts = name[len(LAMP_PREFIX):].split("_")
    if len(parts) == 2 and parts[0].isdigit() and parts[1].isdigit():
        row, col = int(parts[0]), int(parts[1])
        return 12 + row if col == 0 else row * 3 + (col - 1)
    return 0xFFFF


def to_v3(parts, source_name):
    """The v2 part list (local vertices + part-to-model) -> .hmesh v3 bytes: one vertex
    and index buffer in model space (rest pose), a material table, slots for pads and groups."""
    import hmesh_io
    import numpy as np

    materials, material_index = [], {}
    out_parts, vert_chunks, idx_chunks = [], [], []
    base = first = 0
    lo_all, hi_all = np.full(3, 1e9), np.full(3, -1e9)

    for name, matrix, pivot, verts, idx in parts:
        m = np.array(matrix, dtype=np.float64).reshape(4, 4).T  # column-major -> row-major math
        v = np.array(verts, dtype=np.float64).reshape(-1, 14)
        n_vert = len(v)
        pos = (v[:, 0:3] @ m[:3, :3].T + m[:3, 3]) if n_vert else np.zeros((0, 3))
        normal_matrix = np.linalg.inv(m[:3, :3]).T
        nrm = v[:, 3:6] @ normal_matrix.T if n_vert else np.zeros((0, 3))
        nrm = nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-20)

        flags_for_part = 0
        if name == "display_surface":
            flags_for_part |= hmesh_io.MATERIAL_FLAGS["lcd"]
        if name == "chassis_topPlate":
            flags_for_part |= hmesh_io.MATERIAL_FLAGS["decalTarget"]

        mat_ids = np.zeros(n_vert, dtype=np.uint8)
        part_area = {}
        for i in range(n_vert):
            colour = tuple(round(float(c), 6) for c in v[i, 6:9])
            key = (colour, round(float(v[i, 9]), 6), round(float(v[i, 12]), 6), round(float(v[i, 13]), 6), flags_for_part)
            if key not in material_index:
                material_index[key] = len(materials)
                label = next((nm for rgb, nm in PALETTE_NAMES.items()
                              if all(abs(a - b) < 1e-6 for a, b in zip(rgb, colour))), f"m{len(materials)}")
                if flags_for_part & hmesh_io.MATERIAL_FLAGS["lcd"]:
                    label = "lcd"
                elif flags_for_part & hmesh_io.MATERIAL_FLAGS["decalTarget"]:
                    label = "deck"
                materials.append(dict(name=label[:16], base=colour, roughness=key[2], metallic=key[3],
                                      emissive=(key[1] * colour[0], key[1] * colour[1], key[1] * colour[2]),
                                      flags=flags_for_part))
            mat_ids[i] = material_index[key]
        if len(materials) > 255:
            raise ValueError("more than 255 materials")

        idx = np.asarray(idx, dtype=np.int64)
        tri_material = mat_ids[idx[0::3]] if len(idx) else np.zeros(0, np.uint8)
        dominant = int(np.bincount(tri_material).argmax()) if len(tri_material) else 0

        slot = SLOTS.get(name, 0)
        kind = part_kind(name)
        flags = 0
        if len(idx) and name not in ANCHOR_PARTS:
            flags |= hmesh_io.PART_FLAGS["castsShadow"]
        if slot:
            flags |= hmesh_io.PART_FLAGS["moving"]
        if name == "chassis_topPlate":
            flags |= hmesh_io.PART_FLAGS["decal"]

        if n_vert:
            lo, hi = pos.min(axis=0), pos.max(axis=0)
        else:
            t = m[:3, 3]
            lo = hi = t
        if flags & hmesh_io.PART_FLAGS["castsShadow"]:
            lo_all, hi_all = np.minimum(lo_all, lo), np.maximum(hi_all, hi)

        uv = np.clip(v[:, 10:12], 0.0, 1.0) if n_vert else np.zeros((0, 2))
        rec = np.zeros(n_vert, dtype=hmesh_io.VERTEX)
        rec["pos"] = pos
        rec["normal"] = hmesh_io.oct_encode(nrm) if n_vert else np.zeros((0, 2), np.int16)
        rec["uv"] = np.round(uv * 65535).astype(np.uint16)
        rec["material"] = mat_ids
        rec["slot"] = slot
        rec["mask"] = 1 if name == "chassis_topPlate" else 0
        rec["ao"] = 255
        vert_chunks.append(rec)
        idx_chunks.append((idx + base).astype(np.uint32))

        centre = (lo + hi) / 2
        out_parts.append(dict(name=name, kind=kind, flags=flags, slot=slot, material=dominant,
                              pivot=tuple(float(c) for c in centre), axis=(0.0, 0.0, -1.0),
                              travel=KEY_TRAVEL if slot else 0.0,
                              bounds=tuple(float(c) for c in (*lo, *hi)),
                              first_index=first, index_count=len(idx), base_vertex=base, vertex_count=n_vert,
                              led_for=led_for(name) if kind == hmesh_io.KIND["led"] else 0xFFFF))
        base += n_vert
        first += len(idx)

    vertices = np.concatenate(vert_chunks) if vert_chunks else np.zeros(0, dtype=hmesh_io.VERTEX)
    indices = np.concatenate(idx_chunks) if idx_chunks else np.zeros(0, np.uint32)
    meta = dict(generator="tools/model/usdz_to_hmesh.py", format=3, source=source_name,
                parts=len(out_parts), materials=len(materials), vertices=int(len(vertices)),
                triangles=int(len(indices) // 3))
    return hmesh_io.write_hmesh(materials, out_parts, vertices, indices,
                                tuple(float(c) for c in lo_all), tuple(float(c) for c in hi_all), meta)


if __name__ == "__main__":
    import argparse
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("usdz")
    ap.add_argument("out")
    ap.add_argument("--format", choices=["v3", "v2"], default="v3")
    a = ap.parse_args()
    convert(a.usdz, a.out, a.format)
