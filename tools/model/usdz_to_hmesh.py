"""Convert halo's Blender-authored EP40.usdz into a compact .hmesh file for the
Windows app.

The macOS app loads the USDZ through RealityKit. On Windows, we pre-bake the
same geometry once with Pixar's USD library (pip install usd-core) into a
simple binary the C++ renderer can read with no dependencies.

Each direct child of /ep40_root becomes one *part*, named exactly like the prim
(pad_7, group_a, button_play, display_surface, ...). This keeps halo's frozen
entity-name contract (DD-006), so the app can move pads and light LEDs by name.

Format (little endian):
    char[4]  "HMSH"
    u32      version (1)
    u32      partCount
    per part:
        u16      nameLength, then UTF-8 name bytes
        f32[16]  part -> model transform, column-major
        f32[3]   pivot (part-local bounds centre)
        u32      vertexCount
        f32[12]  per vertex: position xyz, normal xyz, colour rgb, emissive, uv
        u32      indexCount
        u32[]    indices

Usage: python usdz_to_hmesh.py <EP40.usdz> <out.hmesh>
"""

import struct
import sys
import tempfile
import zipfile
from pathlib import Path

from pxr import Gf, Usd, UsdGeom, UsdShade


def material_props(prim):
    """Base colour + emissive strength from the bound UsdPreviewSurface."""
    material = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()[0]
    colour, emissive = (0.8, 0.8, 0.8), 0.0
    if not material:
        return colour, emissive
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
        break
    return colour, emissive


def mesh_triangles(mesh, to_part):
    """Yield (position, normal, uv) triples per triangle corner, in part space."""
    points = mesh.GetPointsAttr().Get() or []
    counts = mesh.GetFaceVertexCountsAttr().Get() or []
    indices = mesh.GetFaceVertexIndicesAttr().Get() or []
    normals = mesh.GetNormalsAttr().Get()
    normal_interp = mesh.GetNormalsInterpolation() if normals else None

    st = UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st")
    uvs, uv_interp = None, None
    if st and st.HasValue():
        uvs = st.ComputeFlattened()
        uv_interp = st.GetInterpolation()

    normal_xf = to_part.GetInverse().GetTranspose()
    corner = 0
    for count in counts:
        face = list(range(corner, corner + count))
        ps = [Gf.Vec3d(points[indices[c]]) for c in face]
        flat = Gf.Cross(ps[1] - ps[0], ps[2] - ps[0])
        flat = flat.GetNormalized() if flat.GetLength() > 0 else Gf.Vec3d(0, 1, 0)
        for k in range(1, count - 1):  # fan triangulation
            for c in (face[0], face[k], face[k + 1]):
                p = Gf.Vec3d(points[indices[c]])
                if normal_interp == "faceVarying":
                    n = Gf.Vec3d(normals[c])
                elif normal_interp == "vertex":
                    n = Gf.Vec3d(normals[indices[c]])
                else:
                    n = flat
                if uvs is not None and uv_interp == "faceVarying":
                    uv = uvs[c]
                elif uvs is not None and uv_interp == "vertex":
                    uv = uvs[indices[c]]
                else:
                    uv = (0.0, 0.0)
                wp = to_part.Transform(p)
                wn = normal_xf.TransformDir(n)
                wn = wn.GetNormalized() if wn.GetLength() > 0 else Gf.Vec3d(0, 1, 0)
                yield wp, wn, uv
        corner += count


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
            verts, idx, lo, hi = [], [], [1e9] * 3, [-1e9] * 3
            seen = {}  # identical corners share one vertex
            for prim in Usd.PrimRange(part):
                if not prim.IsA(UsdGeom.Mesh):
                    continue
                if UsdGeom.Imageable(prim).ComputeVisibility() == UsdGeom.Tokens.invisible:
                    continue
                mesh_to_part = (cache.GetLocalToWorldTransform(prim) * root_inv) * model_to_part
                colour, emissive = material_props(prim)
                for p, n, uv in mesh_triangles(UsdGeom.Mesh(prim), mesh_to_part):
                    v = (*p, *n, *colour, emissive, *uv)
                    key = tuple(round(x, 6) for x in v)
                    if key not in seen:
                        seen[key] = len(verts)
                        verts.append(v)
                    idx.append(seen[key])
                    lo = [min(a, b) for a, b in zip(lo, p)]
                    hi = [max(a, b) for a, b in zip(hi, p)]
            if not verts:
                continue
            # Gf matrices are row-major for row vectors; transposing that gives
            # the column-major, column-vector matrix the renderer expects.
            m = part_to_model.GetTranspose()
            column_major = [m[r][c] for c in range(4) for r in range(4)]
            pivot = [(a + b) / 2 for a, b in zip(lo, hi)]
            parts.append((part.GetName(), column_major, pivot, verts, idx))

    with open(out_path, "wb") as out:
        out.write(b"HMSH")
        out.write(struct.pack("<II", 1, len(parts)))
        for name, matrix, pivot, verts, idx in parts:
            encoded = name.encode("utf-8")
            out.write(struct.pack("<H", len(encoded)))
            out.write(encoded)
            out.write(struct.pack("<16f", *matrix))
            out.write(struct.pack("<3f", *pivot))
            out.write(struct.pack("<I", len(verts)))
            for v in verts:
                out.write(struct.pack("<12f", *v))
            out.write(struct.pack("<I", len(idx)))
            out.write(struct.pack(f"<{len(idx)}I", *idx))

    total = sum(len(p[3]) for p in parts)
    print(f"wrote {out_path}: {len(parts)} parts, {total} vertices")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    convert(sys.argv[1], sys.argv[2])
