""".hmesh v3: the stage's model format (docs/redesign/riddim-3d-plan.md section 5),
shared by the model tools and their tests. Source/Core/HmeshFormat.cpp reads it.

The file is the chunked container of htex_io.py (magic "HMSH", version 3, CRC of
everything after the chunk table). The plan's 48-byte header with the bounds
inline became the shared 32-byte header plus a 'HEAD' chunk, so one parser
reads every data file.

Chunks (little-endian):
  'HEAD'  f32[3] boundsMin, f32[3] boundsMax  (over parts flagged castsShadow;
          boundsMin.z is the ground contact), u32 partCount, u32 materialCount,
          u32 vertexCount, u32 indexCount
  'MATL'  materialCount x 72 B: char[16] name; f32[3] base (linear); f32 roughness;
          f32 metallic; f32 clearcoat; f32 clearcoatRoughness; f32 transmission;
          f32 thicknessMm; f32[3] emissive (linear); f32 grain; u32 flags
          flags: 1 glass, 2 lcd, 4 led, 8 apertureEmitter, 16 decalTarget, 32 edgeEmitter
  'PART'  partCount x { u16 nameLength; name; pad to 4; u8 kind; u8 flags; u8 slot;
          u8 material; f32[3] pivot; f32[3] axis; f32 travel (m); f32[6] bounds
          (min xyz, max xyz); u32 firstIndex, indexCount, baseVertex, vertexCount;
          u16 ledFor; u16 reserved }
          kind: 0 static 1 pad 2 group 3 button 4 knob 5 fader 6 led 7 display
                8 glass 9 anchor 10 speakerCone 11 plate
          flags: 1 castsShadow 2 moving 4 emissive 8 transparent 16 decal
  'VERT'  vertexCount x 24 B: f32[3] position (model space, rest pose, metres);
          snorm16[2] normal (octahedral); unorm16[2] uv; u8 material; u8 slot;
          u8 mask (1 decal, 2 grain, 4 glass, 8 emissive); u8 ao (255 = none)
  'INDX'  indexCount x u32, absolute (one draw can cover every part); each
          part's indices lie in [baseVertex, baseVertex + vertexCount)
  'META'  UTF-8 JSON

Model space: Z up, metres, origin at the footprint centre, ports at +Y.
"""

import json
import struct

import numpy as np

import htex_io

KIND = dict(static=0, pad=1, group=2, button=3, knob=4, fader=5, led=6, display=7, glass=8, anchor=9,
            speakerCone=10, plate=11)
PART_FLAGS = dict(castsShadow=1, moving=2, emissive=4, transparent=8, decal=16)
MATERIAL_FLAGS = dict(glass=1, lcd=2, led=4, apertureEmitter=8, decalTarget=16, edgeEmitter=32)
VERTEX = np.dtype([("pos", "<f4", 3), ("normal", "<i2", 2), ("uv", "<u2", 2),
                   ("material", "u1"), ("slot", "u1"), ("mask", "u1"), ("ao", "u1")])
assert VERTEX.itemsize == 24
MATERIAL = struct.Struct("<16s3f6f3ff")  # name, base, rough, metal, cc, ccr, transmission, thickness, emissive, grain; then u32 flags
MATERIAL_SIZE = MATERIAL.size + 4
PART_FIXED = struct.Struct("<BBBB3f3ff6fIIIIHH")


def oct_encode(n):
    """Unit normals [..., 3] -> octahedral snorm16 [..., 2]."""
    n = np.asarray(n, dtype=np.float64)
    n = n / np.maximum(np.abs(n).sum(axis=-1, keepdims=True), 1e-20)
    x, y, z = n[..., 0], n[..., 1], n[..., 2]
    sx, sy = np.where(x >= 0, 1.0, -1.0), np.where(y >= 0, 1.0, -1.0)
    ox = np.where(z < 0, (1.0 - np.abs(y)) * sx, x)
    oy = np.where(z < 0, (1.0 - np.abs(x)) * sy, y)
    return np.round(np.clip(np.stack([ox, oy], axis=-1), -1, 1) * 32767).astype(np.int16)


def oct_decode(e):
    e = np.asarray(e, dtype=np.float64) / 32767.0
    v = np.stack([e[..., 0], e[..., 1], 1.0 - np.abs(e[..., 0]) - np.abs(e[..., 1])], axis=-1)
    neg = v[..., 2] < 0
    sx, sy = np.where(v[..., 0] >= 0, 1.0, -1.0), np.where(v[..., 1] >= 0, 1.0, -1.0)
    vx = np.where(neg, (1.0 - np.abs(v[..., 1])) * sx, v[..., 0])
    vy = np.where(neg, (1.0 - np.abs(v[..., 0])) * sy, v[..., 1])
    v = np.stack([vx, vy, v[..., 2]], axis=-1)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def write_hmesh(materials, parts, vertices, indices, bounds_min, bounds_max, meta):
    """materials: list of dicts (name, base, roughness, metallic, clearcoat, clearcoat_roughness,
    transmission, thickness_mm, emissive, grain, flags); parts: list of dicts (name, kind, flags,
    slot, material, pivot, axis, travel, bounds, first_index, index_count, base_vertex,
    vertex_count, led_for); vertices: structured array of VERTEX; indices: uint32 array."""
    matl = bytearray()
    for m in materials:
        matl += MATERIAL.pack(m["name"].encode()[:16], *m["base"], m["roughness"], m.get("metallic", 0.0),
                              m.get("clearcoat", 0.0), m.get("clearcoat_roughness", 0.0), m.get("transmission", 0.0),
                              m.get("thickness_mm", 0.0), *m.get("emissive", (0.0, 0.0, 0.0)), m.get("grain", 0.0))
        matl += struct.pack("<I", m.get("flags", 0))
    part = bytearray()
    for p in parts:
        name = p["name"].encode("utf-8")
        part += struct.pack("<H", len(name)) + name
        part += b"\0" * (-len(part) % 4)
        part += PART_FIXED.pack(p["kind"], p["flags"], p["slot"], p["material"], *p["pivot"], *p["axis"], p["travel"],
                                *p["bounds"], p["first_index"], p["index_count"], p["base_vertex"],
                                p["vertex_count"], p.get("led_for", 0xFFFF), 0)
    head = struct.pack("<3f3fIIII", *bounds_min, *bounds_max, len(parts), len(materials), len(vertices), len(indices))
    chunks = [(b"HEAD", head), (b"MATL", bytes(matl)), (b"PART", bytes(part)),
              (b"VERT", np.ascontiguousarray(vertices, dtype=VERTEX).tobytes()),
              (b"INDX", np.asarray(indices, dtype="<u4").tobytes()),
              (b"META", json.dumps(meta, sort_keys=True, separators=(",", ":")).encode("utf-8"))]
    return htex_io.write_container(b"HMSH", 3, chunks)


def read_hmesh(data):
    version, _, chunks = htex_io.read_container(data, b"HMSH")
    if version != 3:
        raise ValueError(f"not v3 ({version})")
    h = struct.unpack("<3f3fIIII", chunks[b"HEAD"])
    bounds_min, bounds_max, n_parts, n_mat, n_vert, n_idx = h[0:3], h[3:6], h[6], h[7], h[8], h[9]
    raw = chunks[b"MATL"]
    materials = []
    for i in range(n_mat):
        v = MATERIAL.unpack_from(raw, i * MATERIAL_SIZE)
        (flags,) = struct.unpack_from("<I", raw, i * MATERIAL_SIZE + MATERIAL.size)
        materials.append(dict(name=v[0].rstrip(b"\0").decode(), base=v[1:4], roughness=v[4], metallic=v[5],
                              clearcoat=v[6], clearcoat_roughness=v[7], transmission=v[8], thickness_mm=v[9],
                              emissive=v[10:13], grain=v[13], flags=flags))
    raw, off, parts = chunks[b"PART"], 0, []
    for i in range(n_parts):
        (length,) = struct.unpack_from("<H", raw, off)
        name = raw[off + 2:off + 2 + length].decode("utf-8")
        off += 2 + length
        off += -off % 4
        v = PART_FIXED.unpack_from(raw, off)
        off += PART_FIXED.size
        parts.append(dict(name=name, kind=v[0], flags=v[1], slot=v[2], material=v[3], pivot=v[4:7], axis=v[7:10],
                          travel=v[10], bounds=v[11:17], first_index=v[17], index_count=v[18], base_vertex=v[19],
                          vertex_count=v[20], led_for=v[21]))
    vertices = np.frombuffer(chunks[b"VERT"], dtype=VERTEX, count=n_vert)
    indices = np.frombuffer(chunks[b"INDX"], dtype="<u4", count=n_idx)
    meta = json.loads(chunks[b"META"].decode("utf-8")) if b"META" in chunks else {}
    return dict(bounds_min=bounds_min, bounds_max=bounds_max, materials=materials, parts=parts,
                vertices=vertices, indices=indices, meta=meta)
