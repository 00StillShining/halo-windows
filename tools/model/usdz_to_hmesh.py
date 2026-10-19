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
    vertex, tuned against TE's product photography of the real unit.
  * Rounded-corner chassis slabs in place of the bevelled cubes, and the top row
    of status lamps the Blender model left out.

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

Usage: python usdz_to_hmesh.py <EP40.usdz> <out.hmesh>
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

# Physically based overrides per Blender material, in linear RGB. Colours are
# matched to TE's photography of the EP-40: a warm light-grey satin face, near
# white caps, and the riddim green and orange accents. Anything not listed keeps
# the USD values.
MATERIALS = {
    "m_body":    dict(colour=(0.50, 0.485, 0.44), roughness=0.62),
    "m_plate":   dict(colour=(0.60, 0.585, 0.53), roughness=0.58),
    "m_edge":    dict(colour=(0.30, 0.295, 0.27), roughness=0.66, metallic=0.0),
    "m_cap":     dict(colour=(0.80, 0.79, 0.75), roughness=0.40),
    "m_brand":   dict(colour=(0.84, 0.83, 0.79), roughness=0.52),
    "m_dark":    dict(colour=(0.012, 0.014, 0.013), roughness=0.78),
    "m_ink":     dict(roughness=0.50),
    "m_green":   dict(roughness=0.42),
    "m_greenC":  dict(roughness=0.36),
    "m_greenD":  dict(roughness=0.50),
    "m_orange":  dict(roughness=0.42),
    "m_orangeC": dict(roughness=0.36),
    "m_legendG": dict(roughness=0.55),
    "m_legendL": dict(roughness=0.55),
    "m_legendO": dict(roughness=0.55),
}


def material_props(prim):
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
    override = MATERIALS.get(material.GetPrim().GetName(), {})
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


def cylinder(cx, cy, z0, z1, radius, bevel, segments=32):
    """A small capped cylinder with a rounded top edge (status lamp lens)."""
    return rounded_slab(radius, radius, radius, z0, z1, bevel, 0.0, cx, cy)


# Parts whose bevelled-cube geometry is replaced by procedural rounded slabs.
# The real case has softly radiused corners in plan, not 2 mm chamfers.
CHASSIS = {
    "chassis_base":     lambda: rounded_slab(W / 2, D / 2, 0.0052, -TOP, TOP, 0.0016, 0.0012),
    "chassis_topPlate": lambda: rounded_slab(W / 2 - 0.001, D / 2 - 0.001, 0.0044,
                                             TOP - 0.0002, TOP + 0.0014, 0.00055, 0.0),
    "chassis_edgeBand": lambda: rounded_slab(W / 2 + 0.0002, D / 2 + 0.0002, 0.0054,
                                             -TOP, -TOP + 0.0026, 0.0004, 0.0008),
}

# The Blender model lamps only the three gaps between pad rows; the real unit
# also has a lamp above each pad of the top row (LEVEL / PITCH / TIME ...).
GRID_U = (0.229, 0.367, 0.505, 0.643)
TOP_ROW_LAMP_V = 0.395 + 0.048


def extra_parts():
    """(name, model-space triangles with normals, colour, roughness) to append."""
    parts = []
    for col, uu in enumerate(GRID_U):
        x, y = (uu - 0.5) * W, (TOP_ROW_LAMP_V - 0.5) * D
        z = TOP_FACE + 0.00165
        tris = cylinder(x, y, z - 0.000325, z + 0.000325, 0.00125, 0.00015)
        colour = MATERIALS["m_ink"].get("colour", (0.025, 0.031, 0.029))
        parts.append((f"status_lamp_top_{col}", tris, colour, 0.5))
    return parts


# ------------------------------------------------------------------ conversion
def convert(usdz_path, out_path):
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
            # USD uses row vectors: model = partLocal * part_to_model.
            part_to_model = cache.GetLocalToWorldTransform(part) * root_inv
            model_to_part = part_to_model.GetInverse()
            corners = []  # (position, normal, colour, emissive, uv, roughness, metallic)
            for prim in Usd.PrimRange(part):
                if not prim.IsA(UsdGeom.Mesh):
                    continue
                if UsdGeom.Imageable(prim).ComputeVisibility() == UsdGeom.Tokens.invisible:
                    continue
                mesh_to_part = (cache.GetLocalToWorldTransform(prim) * root_inv) * model_to_part
                colour, emissive, roughness, metallic = material_props(prim)

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

            if corners:
                m = part_to_model.GetTranspose()
                parts.append(pack_part(part.GetName(), m, corners))

        for name, tris, colour, roughness in extra_parts():
            corners = [(p, n, colour, 0.0, (0.0, 0.0), roughness, 0.0)
                       for ps, ns in tris for p, n in zip(ps, ns)]
            parts.append(pack_part(name, Gf.Matrix4d(1.0), corners))

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


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    convert(sys.argv[1], sys.argv[2])
