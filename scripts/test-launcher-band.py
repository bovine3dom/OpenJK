#!/usr/bin/env python3
"""Check the original sprite sheet without the local reference image."""

from pathlib import Path
import hashlib
import runpy
import struct
import unittest

ROOT = Path(__file__).resolve().parent.parent
ART = runpy.run_path(str(ROOT / "scripts/build-launcher-band.py"))


class BandArtTests(unittest.TestCase):
    def test_stored_sheet_matches_source(self):
        stored = (ROOT / "launcher/ui/cantina-player.tga").read_bytes()
        self.assertEqual(stored, ART["build"]())
        self.assertEqual(stored[:3], b"\x00\x00\x02")
        self.assertEqual(struct.unpack_from("<HHBB", stored, 12), (256, 256, 32, 0x28))
        self.assertEqual(len(stored), 18 + 256 * 256 * 4)

    def test_eight_distinct_bounded_poses(self):
        poses = [ART["sprite"](i, member) for member in range(5) for i in range(8)]
        for member in range(5):
            self.assertEqual(len({str(pose) for pose in poses[member * 8:member * 8 + 8]}), 8)
        for pose in poses:
            self.assertEqual(len(pose), 32)
            for row in pose:
                self.assertEqual(len(row), 32)
                self.assertTrue(set(row) <= ART["PALETTE"].keys())
            self.assertEqual(pose[0], ["."] * 32)
            self.assertTrue(any(pixel != "." for pixel in pose[31]))

    def test_approved_lead_idle_is_unchanged(self):
        for pose, expected in enumerate((
            "ac96ae95653315161fc83ee8a411c61def0826c227ecc3d895ada8a3145511c9",
            "1669331fc58bfc5639c15c1f028f4058cd07d87807d06cb6506f181a7be3bc24",
        ), 6):
            pixels = "".join("".join(row) for row in ART["sprite"](pose))
            self.assertEqual(hashlib.sha256(pixels.encode()).hexdigest(), expected)

    def test_horn_lift_is_small(self):
        def bounds(pose):
            points = [(x, y) for y, row in enumerate(ART["sprite"](pose))
                      for x, pixel in enumerate(row) if pixel in "bty"]
            return tuple(f(p[i] for p in points) for i in range(2) for f in (min, max))
        self.assertTrue(all(abs(a - b) <= 1 for a, b in zip(bounds(1), bounds(3))))

    def test_separate_props(self):
        for member in range(5):
            pixels = ART["prop"](member)
            self.assertEqual(len(pixels), 32)
            self.assertTrue(all(len(row) == 32 for row in pixels))
            self.assertEqual(any(p != "." for row in pixels for p in row), member >= 3)

    def test_reference_is_not_an_input(self):
        script = (ROOT / "scripts/build-launcher-band.py").read_text()
        self.assertNotIn("cantina_reference", script)
        self.assertFalse(tuple((ROOT / "launcher").rglob("*.jpeg")))


if __name__ == "__main__":
    unittest.main()
