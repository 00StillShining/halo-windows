"""Readers and writers for the stage's chunked binary data files (.htex textures,
.henv environments), shared by the model compiler and its tests.

Container (little-endian, 4-byte aligned; Source/Core/ChunkFile.h reads it):

    char[4] magic ("HTEX" / "HENV"); u32 version; u32 flags; u32 chunkCount;
    u32 crc32 of every byte after the chunk table; u32 reserved[3]     (32 bytes)
    chunkCount x { char[4] id; u32 offset (from file start); u32 size }
    chunk payloads, each padded to 4 bytes

Unknown chunks are skipped by readers. Data textures are stored raw, never as
PNG: JUCE's PNG loader premultiplies alpha and keeps BGRA (plan D9).

.htex v1 chunks
    'TXHD'  u32 width, u32 height, u32 format, u32 mipCount, u32 flags, char[4] channels
            format: 1 R8, 2 RG8, 3 RGBA8, 4 RG16F, 5 RGBA16F
            channels: what each channel holds, e.g. b"RGBA" or b"GOWC" (green, orange,
            white, charcoal SDFs); flags bit 0 = colour data in sRGB, bit 1 = SDF
    'MIPS'  every mip level, level 0 first, tightly packed rows, top row first

.henv v1 chunks
    'ENVH'  u32 faceSize, u32 mipCount, u32 format (1 = RGB9E5); mip m is prefiltered
            for GGX roughness (m / (mipCount - 1))^2
    'CUBE'  for each mip, for each face (+X -X +Y -Y +Z -Z): (faceSize >> mip)^2 u32
            texels, first row first, in OpenGL cube-map face orientation
    'SH9 '  9 x RGB f32: irradiance SH (already convolved with the cosine lobe) of
            the environment without its key and rim lights, which are applied as
            analytic lights so their light can be shadowed
    'LITE'  u32 count, then per light: char[8] name, f32[3] direction (towards the
            light, world Y-up), f32[3] irradiance at normal incidence, f32 angular
            radius (rad), f32 width, f32 height, f32 distance (m)
    'META'  UTF-8 JSON
"""

import json
import struct
import zlib

import numpy as np

HEADER = struct.Struct("<4sIIII12x")
ENTRY = struct.Struct("<4sII")

TEX_FORMATS = {1: ("R8", 1, np.uint8), 2: ("RG8", 2, np.uint8), 3: ("RGBA8", 4, np.uint8),
               4: ("RG16F", 2, np.float16), 5: ("RGBA16F", 4, np.float16)}


# ------------------------------------------------------------------ container
def write_container(magic, version, chunks, flags=0):
    """chunks: list of (id bytes[4], payload bytes). Returns the file bytes."""
    table_size = ENTRY.size * len(chunks)
    offset = HEADER.size + table_size
    entries, body = [], bytearray()
    for cid, payload in chunks:
        assert len(cid) == 4
        entries.append(ENTRY.pack(cid, offset + len(body), len(payload)))
        body += payload
        body += b"\0" * (-len(payload) % 4)
    crc = zlib.crc32(bytes(body)) & 0xFFFFFFFF
    return HEADER.pack(magic, version, flags, len(chunks), crc) + b"".join(entries) + bytes(body)


def read_container(data, magic):
    m, version, flags, count, crc = HEADER.unpack_from(data, 0)
    if m != magic:
        raise ValueError(f"bad magic {m!r}")
    table_end = HEADER.size + ENTRY.size * count
    if (zlib.crc32(data[table_end:]) & 0xFFFFFFFF) != crc:
        raise ValueError("CRC mismatch")
    chunks = {}
    for i in range(count):
        cid, off, size = ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
        if off + size > len(data):
            raise ValueError(f"chunk {cid!r} overruns the file")
        chunks[cid] = data[off:off + size]
    return version, flags, chunks


# ------------------------------------------------------------------ RGB9E5
def rgb9e5_encode(rgb):
    """float32 [..., 3] (non-negative) -> uint32, per the GL_EXT_texture_shared_exponent spec."""
    rgb = np.clip(np.nan_to_num(np.asarray(rgb, dtype=np.float64)), 0.0, 65408.0)
    maxc = rgb.max(axis=-1)
    exp_shared = np.maximum(-16, np.floor(np.log2(np.maximum(maxc, 1e-30)))) + 1 + 15
    denom = np.exp2(exp_shared - 15 - 9)
    maxm = np.floor(maxc / denom + 0.5)
    bump = maxm == 512
    denom = np.where(bump, denom * 2, denom)
    exp_shared = np.where(bump, exp_shared + 1, exp_shared)
    m = np.floor(rgb / denom[..., None] + 0.5).astype(np.uint32)
    m = np.minimum(m, 511)
    e = exp_shared.astype(np.uint32)
    return (m[..., 0] | (m[..., 1] << 9) | (m[..., 2] << 18) | (e << 27)).astype(np.uint32)


def rgb9e5_decode(v):
    v = np.asarray(v, dtype=np.uint32)
    e = (v >> 27).astype(np.float64)
    scale = np.exp2(e - 15 - 9)
    return np.stack([(v & 511) * scale, ((v >> 9) & 511) * scale, ((v >> 18) & 511) * scale], axis=-1)


# ------------------------------------------------------------------ .htex
def write_htex(mips, fmt, channels=b"RGBA", flags=0):
    """mips: list of arrays [h, w, c] (level 0 first) in the format's dtype."""
    _, nch, dtype = TEX_FORMATS[fmt]
    h, w = mips[0].shape[:2]
    payload = bytearray()
    for level in mips:
        a = np.ascontiguousarray(level, dtype=dtype).reshape(level.shape[0], level.shape[1], nch)
        payload += a.astype(np.dtype(dtype).newbyteorder("<")).tobytes()
    header = struct.pack("<IIIII4s", w, h, fmt, len(mips), flags, channels)
    return write_container(b"HTEX", 1, [(b"TXHD", header), (b"MIPS", bytes(payload))])


def read_htex(data):
    _, _, chunks = read_container(data, b"HTEX")
    w, h, fmt, mip_count, flags, channels = struct.unpack("<IIIII4s", chunks[b"TXHD"])
    _, nch, dtype = TEX_FORMATS[fmt]
    raw, mips, off = chunks[b"MIPS"], [], 0
    for m in range(mip_count):
        mw, mh = max(1, w >> m), max(1, h >> m)
        n = mw * mh * nch * np.dtype(dtype).itemsize
        mips.append(np.frombuffer(raw, dtype=dtype, count=mw * mh * nch, offset=off).reshape(mh, mw, nch))
        off += n
    return dict(width=w, height=h, format=fmt, flags=flags, channels=channels, mips=mips)


# ------------------------------------------------------------------ .henv
def write_henv(face_size, cube_mips, sh9, lights, meta):
    """cube_mips: list (per mip) of float arrays [6, n, n, 3]; sh9: [9, 3];
    lights: list of dicts (name, direction, irradiance, angular_radius, width, height, distance)."""
    cube = bytearray()
    for level in cube_mips:
        cube += rgb9e5_encode(level).astype("<u4").tobytes()
    lite = struct.pack("<I", len(lights))
    for l in lights:
        lite += struct.pack("<8s3f3f4f", l["name"].encode()[:8], *l["direction"], *l["irradiance"],
                            l["angular_radius"], l["width"], l["height"], l["distance"])
    chunks = [
        (b"ENVH", struct.pack("<III", face_size, len(cube_mips), 1)),
        (b"CUBE", bytes(cube)),
        (b"SH9 ", np.asarray(sh9, dtype="<f4").tobytes()),
        (b"LITE", lite),
        (b"META", json.dumps(meta, sort_keys=True, separators=(",", ":")).encode("utf-8")),
    ]
    return write_container(b"HENV", 1, chunks)


def read_henv(data):
    _, _, chunks = read_container(data, b"HENV")
    face, mip_count, fmt = struct.unpack("<III", chunks[b"ENVH"])
    raw = np.frombuffer(chunks[b"CUBE"], dtype="<u4")
    mips, off = [], 0
    for m in range(mip_count):
        n = max(1, face >> m)
        mips.append(rgb9e5_decode(raw[off:off + 6 * n * n]).reshape(6, n, n, 3))
        off += 6 * n * n
    sh9 = np.frombuffer(chunks[b"SH9 "], dtype="<f4").reshape(9, 3)
    lite = chunks[b"LITE"]
    (count,) = struct.unpack_from("<I", lite, 0)
    lights = []
    for i in range(count):
        name, *v = struct.unpack_from("<8s3f3f4f", lite, 4 + i * 48)
        lights.append(dict(name=name.rstrip(b"\0").decode(), direction=v[0:3], irradiance=v[3:6],
                           angular_radius=v[6], width=v[7], height=v[8], distance=v[9]))
    meta = json.loads(chunks[b"META"].decode("utf-8")) if b"META" in chunks else {}
    return dict(face_size=face, mips=mips, sh9=sh9, lights=lights, meta=meta)
