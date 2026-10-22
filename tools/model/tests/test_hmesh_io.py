"""python -I -m unittest discover tools/model/tests  (from the repo root)"""

import sys
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hmesh_io  # noqa: E402


def tiny():
    materials = [dict(name="cream", base=(0.716, 0.644, 0.521), roughness=0.62),
                 dict(name="deck", base=(0.716, 0.644, 0.521), roughness=0.62, flags=hmesh_io.MATERIAL_FLAGS["decalTarget"])]
    v = np.zeros(3, dtype=hmesh_io.VERTEX)
    v["pos"] = [(0, 0, 0), (0.01, 0, 0), (0, 0.01, 0.005)]
    v["normal"] = hmesh_io.oct_encode(np.array([[0, 0, 1.0]] * 3))
    v["uv"] = [(0, 0), (65535, 0), (0, 65535)]
    v["material"] = 1
    v["slot"] = 1
    v["ao"] = 255
    parts = [dict(name="pad_7", kind=1, flags=3, slot=1, material=1, pivot=(0, 0, 0), axis=(0, 0, -1), travel=0.0026,
                  bounds=(0, 0, 0, 0.01, 0.01, 0.005), first_index=0, index_count=3, base_vertex=0, vertex_count=3),
             dict(name="halo_ring", kind=9, flags=0, slot=0, material=0, pivot=(0, 0, 0), axis=(0, 0, -1), travel=0.0,
                  bounds=(0, 0, 0, 0, 0, 0), first_index=3, index_count=0, base_vertex=3, vertex_count=0)]
    return materials, parts, v, np.array([0, 1, 2], np.uint32)


class HmeshIo(unittest.TestCase):
    def test_round_trip(self):
        m, p, v, i = tiny()
        h = hmesh_io.read_hmesh(hmesh_io.write_hmesh(m, p, v, i, (0, 0, 0), (0.01, 0.01, 0.005), {"x": 1}))
        self.assertEqual([x["name"] for x in h["materials"]], ["cream", "deck"])
        self.assertEqual(h["materials"][1]["flags"], hmesh_io.MATERIAL_FLAGS["decalTarget"])
        self.assertAlmostEqual(h["materials"][0]["roughness"], 0.62, places=6)
        self.assertEqual([x["name"] for x in h["parts"]], ["pad_7", "halo_ring"])
        self.assertEqual(h["parts"][0]["slot"], 1)
        self.assertAlmostEqual(h["parts"][0]["travel"], 0.0026, places=6)
        np.testing.assert_array_equal(h["vertices"], v)
        np.testing.assert_array_equal(h["indices"], i)
        self.assertEqual(h["meta"], {"x": 1})

    def test_output_is_byte_deterministic(self):
        a = hmesh_io.write_hmesh(*tiny(), (0, 0, 0), (1, 1, 1), {"b": 2, "a": 1})
        b = hmesh_io.write_hmesh(*tiny(), (0, 0, 0), (1, 1, 1), {"a": 1, "b": 2})
        self.assertEqual(a, b)

    def test_octahedral_normals(self):
        rng = np.random.default_rng(3)
        n = rng.normal(size=(2000, 3))
        n /= np.linalg.norm(n, axis=1, keepdims=True)
        back = hmesh_io.oct_decode(hmesh_io.oct_encode(n))
        self.assertLess(np.max(np.linalg.norm(back - n, axis=1)), 2e-4)

    def test_shipped_asset(self):
        path = ROOT / "assets" / "stage" / "EP40.hmesh"
        if not path.exists():
            self.skipTest("no asset")
        h = hmesh_io.read_hmesh(path.read_bytes())
        self.assertEqual(sum(1 for p in h["parts"] if p["slot"]), 16)
        self.assertTrue(np.all(h["indices"] < len(h["vertices"])))
        self.assertTrue(np.all(h["vertices"]["material"] < len(h["materials"])))


if __name__ == "__main__":
    unittest.main()
