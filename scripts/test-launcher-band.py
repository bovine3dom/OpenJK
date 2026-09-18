#!/usr/bin/env python3
"""Check the original sprite sheet without the local reference image."""

from pathlib import Path
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
        self.assertEqual(struct.unpack_from("<HHBB", stored, 12), (256, 32, 32, 0x28))
        self.assertEqual(len(stored), 18 + 256 * 32 * 4)

    def test_eight_distinct_bounded_poses(self):
        poses = [ART["sprite"](i) for i in range(8)]
        self.assertEqual(len({str(pose) for pose in poses}), 8)
        for pose in poses:
            self.assertEqual(len(pose), 32)
            for row in pose:
                self.assertEqual(len(row), 32)
                self.assertTrue(set(row) <= ART["PALETTE"].keys())
            self.assertEqual(pose[0], ["."] * 32)
            self.assertTrue(any(pixel != "." for pixel in pose[31]))

    def test_reference_is_not_an_input(self):
        script = (ROOT / "scripts/build-launcher-band.py").read_text()
        self.assertNotIn("cantina_reference", script)
        self.assertFalse(tuple((ROOT / "launcher").rglob("*.jpeg")))


if __name__ == "__main__":
    unittest.main()
