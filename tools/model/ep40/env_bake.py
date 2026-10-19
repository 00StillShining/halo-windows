"""Bakes the stage's studio environment into assets/stage/studio.henv
(docs/redesign/riddim-3d-plan.md section 3.4).

numpy only, deterministic (Hammersley sequences, float64, no clocks): two runs
give byte-identical files. The scene is analytic, so the prefilter samples the
studio itself rather than a source cube map.

  Dark warm room   everywhere                                 radiance 0.05
  Key softbox      0.9 x 0.6 m, upper front-left, 1.2 m        radiance 12
  Rim strip        1.0 x 0.02 m, behind, tilted, 1.2 m         radiance 100
  Fill card        1.2 x 1.2 m, front-left, low, 1.5 m         radiance 1.4

(The plan's starting values were 0.03 / 6 / 10 over 1.0 x 0.12 m / 0.8; at
those the cream deck lit at L* 71, under TE's look, and a 0.12 m strip was
taller than the screen's reflection window, so it washed the whole glass
instead of crossing it. The thinner rim is brighter, a little above the plan's
1.0 x 0.12 x 10, so RECORD and PLAY carry the broad highlight the plan asks for.)

Every source has a soft gradient falloff at its edges. The rim strip sits behind
the device at the elevation the hero camera's reflections look at (about 37
degrees, slightly left), tilted so its reflection crosses the screen window as
one long diagonal streak, the cue TE draws on the EP-40's glass.

The bake is neutral: no floor bounce, because the floor colour is a runtime look
token (the stage adds that SH lobe on every setLook). The key and rim are
described in 'LITE' and left out of the SH9 irradiance: the renderer lights
diffuse with them analytically, so the key can be shadowed; the prefiltered
cube map keeps them for reflections.

World frame: the stage's (Y up, metres; the player at +Z, the ports at -Z).

    python -I tools/model/ep40/env_bake.py --out assets/stage/studio.henv [--preview build/model/studio.png]
"""

import argparse
import hashlib
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import htex_io  # noqa: E402

FACE_SIZE = 256        # fine enough that the glass shows a crisp, thin rim streak
MIP_COUNT = 7          # 256 .. 4; mip m is GGX roughness (m / (MIP_COUNT - 1))^2, so the
                       # renderer samples lod = sqrt(roughness) x maxMip: glossy acrylic
                       # (0.05) lands near mip 1 instead of blending in mip 1's 0.2 haze
SAMPLES = 1024         # per texel, per prefiltered mip
SH_SAMPLES = 1 << 18   # Fibonacci directions for the SH projection


def normalise(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


class Emitter:
    """A rectangular soft light facing the origin."""

    def __init__(self, name, direction, distance, width, height, radiance, colour, tilt_degrees=0.0, edge=0.08):
        self.name = name
        self.c = normalise(direction)
        self.distance = distance
        self.hw, self.hh = width / 2, height / 2
        self.radiance = radiance
        self.colour = np.asarray(colour, dtype=np.float64)
        self.edge = edge  # soft falloff, as a fraction of the smaller half size
        up = np.array([0.0, 1.0, 0.0])
        u = np.cross(up, self.c)
        if np.linalg.norm(u) < 1e-6:
            u = np.array([1.0, 0.0, 0.0])
        u = normalise(u)
        v = np.cross(self.c, u)
        t = math.radians(tilt_degrees)
        self.u = u * math.cos(t) + v * math.sin(t)
        self.v = np.cross(self.c, self.u)

    def mask(self, a, b):
        e = self.edge * min(self.hw, self.hh) + 1e-9

        def soft(x):  # 0 outside, 1 inside, smooth across +-e
            t = np.clip((x / e + 1.0) * 0.5, 0.0, 1.0)
            return t * t * (3.0 - 2.0 * t)

        inside = soft(self.hw - np.abs(a)) * soft(self.hh - np.abs(b))
        # A gentle centre-weighted gradient, like a real diffusion panel.
        r2 = (a / self.hw) ** 2 + (b / self.hh) ** 2
        return inside * (1.0 - 0.18 * np.clip(r2, 0.0, 1.5))

    def __call__(self, d):
        cosang = d @ self.c
        out = np.zeros(d.shape[:-1] + (3,))
        front = cosang > 1e-6
        if not np.any(front):
            return out
        t = self.distance / np.where(front, cosang, 1.0)
        hit = d * t[..., None] - self.c * self.distance
        a, b = hit @ self.u, hit @ self.v
        m = np.where(front, self.mask(a, b), 0.0)
        return m[..., None] * self.colour * self.radiance

    def irradiance(self):
        """Irradiance at the origin on a surface facing the light: the
        integral of its radiance over solid angle, by midpoint quadrature."""
        n = 600
        e = self.edge * min(self.hw, self.hh)
        a = np.linspace(-self.hw - e, self.hw + e, n)
        b = np.linspace(-self.hh - e, self.hh + e, n)
        da, db = a[1] - a[0], b[1] - b[0]
        A, B = np.meshgrid(a, b, indexing="ij")
        p = self.c * self.distance + A[..., None] * self.u + B[..., None] * self.v
        r = np.linalg.norm(p, axis=-1)
        cos_emit = (p @ self.c) / r  # the panel faces the origin
        solid = cos_emit * da * db / r ** 2
        return (self.mask(A, B) * solid).sum() * self.colour * self.radiance


ROOM = np.array([1.0, 0.95, 0.88]) * 0.05

KEY = Emitter("key", (-0.45, 1.0, 0.55), 1.2, 0.9, 0.6, 12.0, (1.0, 0.97, 0.92))
RIM = Emitter("rim", (-0.30, 0.60, -0.74), 1.2, 1.0, 0.02, 100.0, (0.95, 0.97, 1.0), tilt_degrees=-32.0, edge=0.3)
FILL = Emitter("fill", (-0.80, 0.25, 0.60), 1.5, 1.2, 1.2, 1.4, (0.92, 0.95, 1.0))


def radiance(d, with_direct=True):
    out = np.broadcast_to(ROOM, d.shape[:-1] + (3,)).copy()
    out += FILL(d)
    if with_direct:
        out += KEY(d) + RIM(d)
    return out


# ------------------------------------------------------------------ cube maps
def face_directions(face, n, offset=(0.5, 0.5)):
    """Unit directions through the texel grid of one OpenGL cube face (row 0 first)."""
    s = (np.arange(n) + offset[0]) / n * 2.0 - 1.0
    t = (np.arange(n) + offset[1]) / n * 2.0 - 1.0
    sc, tc = np.meshgrid(s, t)  # [row = t][col = s]
    one = np.ones_like(sc)
    d = {
        0: (one, -tc, -sc),   # +X
        1: (-one, -tc, sc),   # -X
        2: (sc, one, tc),     # +Y
        3: (sc, -one, -tc),   # -Y
        4: (sc, -tc, one),    # +Z
        5: (-sc, -tc, -one),  # -Z
    }[face]
    return normalise(np.stack(d, axis=-1))


def hammersley(n):
    i = np.arange(n, dtype=np.uint64)
    bits = i.copy()
    bits = ((bits << 16) | (bits >> 16)) & 0xFFFFFFFF
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1)
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2)
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4)
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8)
    return i.astype(np.float64) / n, bits.astype(np.float64) / 4294967296.0


def prefilter(normals, roughness, samples=SAMPLES, chunk=512):
    """GGX-prefiltered radiance around each normal (N = V = R), Karis' split sum."""
    alpha = roughness * roughness
    u1, u2 = hammersley(samples)
    phi = 2.0 * np.pi * u1
    cos_t = np.sqrt((1.0 - u2) / (1.0 + (alpha * alpha - 1.0) * u2))
    sin_t = np.sqrt(1.0 - cos_t * cos_t)
    h_local = np.stack([sin_t * np.cos(phi), sin_t * np.sin(phi), cos_t], axis=-1)  # [S, 3]

    flat = normals.reshape(-1, 3)
    out = np.zeros_like(flat)
    for start in range(0, len(flat), chunk):
        n = flat[start:start + chunk]                                  # [T, 3]
        up = np.where(np.abs(n[:, 2:3]) < 0.999, [[0.0, 0.0, 1.0]], [[1.0, 0.0, 0.0]])
        tx = normalise(np.cross(up, n))
        ty = np.cross(n, tx)
        h = h_local[None, :, 0:1] * tx[:, None] + h_local[None, :, 1:2] * ty[:, None] + h_local[None, :, 2:3] * n[:, None]
        vh = np.sum(n[:, None] * h, axis=-1, keepdims=True)
        l = 2.0 * vh * h - n[:, None]                                  # [T, S, 3]
        nol = np.sum(n[:, None] * l, axis=-1)
        w = np.where(nol > 0.0, nol, 0.0)
        col = radiance(normalise(l))
        out[start:start + chunk] = (col * w[..., None]).sum(axis=1) / np.maximum(w.sum(axis=1), 1e-12)[:, None]
    return out.reshape(normals.shape)


def bake_cube():
    mips = []
    for m in range(MIP_COUNT):
        n = FACE_SIZE >> m
        faces = []
        for f in range(6):
            if m == 0:
                # Roughness 0: the studio itself, 4x4 supersampled per texel.
                acc = np.zeros((n, n, 3))
                for oy in range(4):
                    for ox in range(4):
                        acc += radiance(face_directions(f, n, ((ox + 0.5) / 4, (oy + 0.5) / 4)))
                faces.append(acc / 16.0)
            else:
                faces.append(prefilter(face_directions(f, n), (m / (MIP_COUNT - 1)) ** 2))
        mips.append(np.stack(faces))
        print(f"  mip {m}: {n}x{n}", flush=True)
    return mips


# ------------------------------------------------------------------ SH9
def sh9_basis(d):
    x, y, z = d[..., 0], d[..., 1], d[..., 2]
    return np.stack([
        0.282095 * np.ones_like(x),
        0.488603 * y, 0.488603 * z, 0.488603 * x,
        1.092548 * x * y, 1.092548 * y * z, 0.315392 * (3.0 * z * z - 1.0),
        1.092548 * x * z, 0.546274 * (x * x - y * y)], axis=-1)


def fibonacci_sphere(n):
    i = np.arange(n, dtype=np.float64) + 0.5
    y = 1.0 - 2.0 * i / n
    r = np.sqrt(1.0 - y * y)
    phi = i * math.pi * (3.0 - math.sqrt(5.0))
    return np.stack([r * np.cos(phi), y, r * np.sin(phi)], axis=-1)


def bake_sh9():
    """Irradiance SH of the ambient studio (room and fill; no key, no rim)."""
    d = fibonacci_sphere(SH_SAMPLES)
    radiance_sh = (4.0 * math.pi / len(d)) * sh9_basis(d).T @ radiance(d, with_direct=False)  # [9, 3]
    cosine = np.array([math.pi] + [2.0 * math.pi / 3.0] * 3 + [math.pi / 4.0] * 5)
    return radiance_sh * cosine[:, None]


def light_record(e):
    return dict(name=e.name, direction=[float(v) for v in e.c], irradiance=[float(v) for v in e.irradiance()],
                angular_radius=float(math.sqrt(4.0 * e.hw * e.hh / math.pi) / e.distance),
                width=2.0 * e.hw, height=2.0 * e.hh, distance=e.distance)


def preview(path, mips):
    from PIL import Image
    h, w = 256, 512
    lon = (np.arange(w) + 0.5) / w * 2 * np.pi - np.pi
    lat = np.pi / 2 - (np.arange(h) + 0.5) / h * np.pi
    LON, LAT = np.meshgrid(lon, lat)
    d = np.stack([np.cos(LAT) * np.sin(LON), np.sin(LAT), -np.cos(LAT) * np.cos(LON)], axis=-1)
    img = radiance(d)
    img = img / (1.0 + img)  # Reinhard, preview only
    img = np.where(img <= 0.0031308, img * 12.92, 1.055 * np.power(img, 1 / 2.4) - 0.055)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).save(path)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="assets/stage/studio.henv")
    ap.add_argument("--preview")
    args = ap.parse_args(argv)

    print("baking studio environment", flush=True)
    mips = bake_cube()
    sh9 = bake_sh9()
    lights = [light_record(KEY), light_record(RIM)]
    meta = dict(generator="tools/model/ep40/env_bake.py", version=1, face_size=FACE_SIZE, mips=MIP_COUNT,
                mip_roughness="(mip / (mips - 1))^2",
                samples=SAMPLES, room=ROOM.tolist(),
                emitters={e.name: dict(direction=e.c.tolist(), distance=e.distance, width=2 * e.hw,
                                       height=2 * e.hh, radiance=e.radiance) for e in (KEY, RIM, FILL)},
                sh9="irradiance (cosine-convolved) of room + fill; key and rim are analytic")
    data = htex_io.write_henv(FACE_SIZE, mips, sh9, lights, meta)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"wrote {out} ({len(data)} bytes) sha256 {hashlib.sha256(data).hexdigest()}")
    for l in lights:
        print(f"  {l['name']}: dir {np.round(l['direction'], 3).tolist()} E {np.round(l['irradiance'], 3).tolist()}")
    print(f"  SH9 L00 irradiance {np.round(sh9[0] * 0.282095, 3).tolist()}")
    if args.preview:
        preview(args.preview, mips)


if __name__ == "__main__":
    main()
