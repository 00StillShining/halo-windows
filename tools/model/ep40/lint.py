"""Checks a compiled EP-40 model against ep40/spec.py (plan section 4.6, the
parts that apply so far). Exit status 1 on any failure.

    python -I tools/model/ep40/lint.py assets/stage/EP40.hmesh
"""

import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import hmesh_io  # noqa: E402
import spec  # noqa: E402

CONTRACT = """chassis_base chassis_topPlate chassis_edgeBand display_surface speaker_grille halo_ring mic_port
group_a group_b group_c group_d pad_dot pad_0 pad_1 pad_2 pad_3 pad_4 pad_5 pad_6 pad_7 pad_8 pad_9 pad_enter
knob_volume knob_x knob_y fader_track fader_cap button_sound button_main button_tempo button_sample button_keys
button_timing button_fx button_erase button_shift button_minus button_plus button_record button_play
ports_strip ports_usb""".split()


def ref_z(model_z):
    return model_z * 1000.0 + 8.0


def main(path):
    h = hmesh_io.read_hmesh(Path(path).read_bytes())
    parts = {p["name"]: p for p in h["parts"]}
    v, idx = h["vertices"], h["indices"]
    failures = []

    def check(ok, what):
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            failures.append(what)

    def part_points(name, material_ids=None):
        p = parts[name]
        ids = idx[p["first_index"]:p["first_index"] + p["index_count"]]
        pts = v["pos"][ids]
        if material_ids is not None:
            mats = v["material"][ids]
            pts = pts[np.isin(mats, list(material_ids))]
        return pts.astype(np.float64)

    check(len(CONTRACT) == 43 and all(n in parts for n in CONTRACT), "all 43 contract names present")
    lo, hi = np.array(h["bounds_min"]), np.array(h["bounds_max"])
    size = (hi - lo) * 1000
    check(abs(size[0] - spec.WIDTH.mm) <= 0.3, f"footprint width {size[0]:.2f} mm (176 +-0.3)")
    check(abs((parts['chassis_edgeBand']['bounds'][4] - parts['chassis_edgeBand']['bounds'][1]) * 1000 - spec.DEPTH.mm) <= 0.3,
          "body depth 240 +-0.3")
    check(abs(ref_z(lo[2])) <= 0.05, f"ground contact at reference z {ref_z(lo[2]):.2f} (feet flush; plan allows -0.3)")

    body_top = ref_z(parts["chassis_topPlate"]["bounds"][5])
    check(abs(body_top - spec.UPPER_TOP.mm) <= 0.1, f"upper section top z {body_top:.2f} (16.0)")
    for name, _, _ in spec.FULL_KEYS:
        b = parts[name]["bounds"]
        base, top = ref_z(b[2]), ref_z(b[5])
        check(abs(base - spec.DECK_TOP.mm) <= 0.05, f"{name} stands on the deck (base z {base:.2f})")
        # Legends ride a little above the cap top; the cap itself is deck + 3.2.
        cap = part_points(name)
        top_cap = ref_z(np.percentile(cap[:, 2], 99))
        check(abs(top_cap - spec.DECK_TOP.mm - spec.KEY_HEIGHT.mm) <= 0.1, f"{name} top {top_cap:.2f} (deck + 3.2, printed legends included)")

    for name, (kx, ky) in spec.KNOBS.items():
        b = parts[name]["bounds"]
        knob_front = 120.0 - b[1] * 1000  # reference y of the knob's front edge
        check(knob_front <= 129.0, f"{name} ends at y {knob_front:.1f}, clear of the X / Y legends (129.3)")
        top = ref_z(b[5]) - spec.DECK_TOP.mm
        check(abs(top - spec.KNOB_CAP[1]) <= 0.3, f"{name} top {top:.2f} above the deck (11.45)")

    for p in h["parts"]:
        if p["name"].startswith("status_lamp_"):
            check(abs(ref_z(p["bounds"][2]) - spec.DECK_TOP.mm - 0.02) < 0.01, f"{p['name']} flush on the deck")

    tris = len(idx) // 3
    check(tris <= 150000, f"{tris} triangles (<= 150k)")
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "assets/stage/EP40.hmesh"))
