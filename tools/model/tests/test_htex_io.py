"""python -I -m unittest discover tools/model/tests  (from the repo root)"""

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ep40"))
import htex_io  # noqa: E402


class Rgb9e5(unittest.TestCase):
    def test_known_vectors(self):
        # Matched by the C++ decoder in Tests/HenvFormatTests.cpp.
        cases = {(0, 0, 0): 0x0, (1, 1, 1): 0x84020100, (0.5, 0.25, 2.0): 0x8C004040, (65408, 0, 0): 0xF80001FF}
        for rgb, packed in cases.items():
            self.assertEqual(int(htex_io.rgb9e5_encode(np.array(rgb, float))), packed)
            np.testing.assert_allclose(htex_io.rgb9e5_decode(packed), rgb)

    def test_round_trip_error_is_small(self):
        rng = np.random.default_rng(1)
        v = rng.uniform(0, 50, (1000, 3))
        back = htex_io.rgb9e5_decode(htex_io.rgb9e5_encode(v))
        # 9-bit mantissa against the largest channel.
        self.assertTrue(np.all(np.abs(back - v) <= v.max(axis=1, keepdims=True) / 256))


class Container(unittest.TestCase):
    def test_htex_round_trip_keeps_channels_unpremultiplied(self):
        level0 = np.zeros((2, 4, 4), np.uint8)
        level0[1, 3] = (3, 101, 30, 40)
        data = htex_io.write_htex([level0, np.zeros((1, 2, 4), np.uint8), np.zeros((1, 1, 4), np.uint8)], 3, b"GOWC", 2)
        t = htex_io.read_htex(data)
        self.assertEqual(t["channels"], b"GOWC")
        self.assertEqual(tuple(t["mips"][0][1, 3]), (3, 101, 30, 40))

    def test_crc_catches_a_flip(self):
        data = bytearray(htex_io.write_container(b"HTEX", 1, [(b"ABCD", b"hello")]))
        data[-4] ^= 1
        with self.assertRaises(ValueError):
            htex_io.read_container(bytes(data), b"HTEX")

    def test_henv_round_trip(self):
        mips = [np.full((6, 4, 4, 3), 1.0), np.full((6, 2, 2, 3), 0.5), np.full((6, 1, 1, 3), 0.25)]
        lights = [dict(name="key", direction=[0, 1, 0], irradiance=[1, 2, 3], angular_radius=0.3, width=0.9,
                       height=0.6, distance=1.2)]
        env = htex_io.read_henv(htex_io.write_henv(4, mips, np.arange(27).reshape(9, 3), lights, {"a": 1}))
        self.assertEqual(env["face_size"], 4)
        self.assertEqual(len(env["mips"]), 3)
        np.testing.assert_allclose(env["mips"][1], 0.5)
        np.testing.assert_allclose(env["sh9"][8], [24, 25, 26])
        self.assertEqual(env["lights"][0]["name"], "key")
        self.assertAlmostEqual(env["lights"][0]["distance"], 1.2, places=6)


class CubeFaces(unittest.TestCase):
    def test_faces_follow_the_opengl_major_axis_table(self):
        import env_bake
        # Each texel's direction must select its own face under the GL rules,
        # and land back on the same (s, t).
        for face in range(6):
            d = env_bake.face_directions(face, 8)
            ax = np.abs(d)
            major = np.argmax(ax, axis=-1)
            sign = np.take_along_axis(d, major[..., None], -1)[..., 0] > 0
            expected = {0: (0, True), 1: (0, False), 2: (1, True), 3: (1, False), 4: (2, True), 5: (2, False)}[face]
            self.assertTrue(np.all(major == expected[0]))
            self.assertTrue(np.all(sign == expected[1]))
        # +X face, first row: t = -1 side, which is +Y (tc = -ry).
        self.assertGreater(env_bake.face_directions(0, 8)[0, 4, 1], 0)


if __name__ == "__main__":
    unittest.main()
