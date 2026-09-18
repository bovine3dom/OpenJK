#!/usr/bin/env python3
"""Test the music converter without the source MIDI."""

from pathlib import Path
import runpy
import struct
import unittest

ROOT = Path(__file__).resolve().parent.parent
CONVERTER = runpy.run_path(str(ROOT / "scripts/compile-launcher-music.py"))


def midi(*tracks):
    header = b"MThd" + struct.pack(">IHHH", 6, int(len(tracks) > 1), len(tracks), 480)
    return header + b"".join(b"MTrk" + struct.pack(">I", len(track)) + track for track in tracks)


class MusicTests(unittest.TestCase):
    def test_tempo_map_across_tracks(self):
        tempo = b"\x00\xff\x51\x03\x07\xa1\x20\x83\x60\xff\x51\x03\x0f\x42\x40"
        melody = b"\x00\x90\x3c\x64\x87\x40\x80\x3c\x00"
        notes = CONVERTER["read_score"](midi(tempo, melody))
        self.assertEqual(len(notes), 1)
        self.assertEqual(notes[0][:3], (0, 1.5, 60))

    def test_program_pan_running_status_and_velocity_zero(self):
        score = midi(b"\x00\xc0\x20\x00\xb0\x0a\x00\x00\x90\x30\x64"
                     b"\x00\x34\x50\x83\x60\x30\x00\x00\x34\x00")
        notes = CONVERTER["read_score"](score)
        self.assertEqual(len(notes), 2)
        self.assertEqual(notes[0][4:], (32, 0, 0))
        self.assertEqual(notes[0][1], 0.5)
        self.assertGreater(notes[0][3], notes[1][3])

    def test_sustain_release(self):
        score = midi(b"\x00\xb0\x40\x7f\x00\x90\x3c\x64"
                     b"\x83\x60\x80\x3c\x00\x83\x60\xb0\x40\x00")
        self.assertEqual(CONVERTER["read_score"](score)[0][1], 1.0)

    def test_invalid_files(self):
        for data in (b"", midi(b"\x00\x90\x3c"), midi(b"\x00\x3c\x64"), midi(b"")):
            with self.subTest(data=data), self.assertRaises(ValueError):
                CONVERTER["read_score"](data)

    def test_compilation_preserves_voice_controls(self):
        notes = [(0.1, 0.2, 60, 0.5, 0, 0, 0), (0, 0.1, 48, 0.5, 32, 1, 1)]
        score = CONVERTER["compile_score"](notes)
        self.assertEqual(score, CONVERTER["compile_score"](notes))
        self.assertEqual(struct.unpack_from("<8sII", score), (b"OJCHIP01", 6174, 2))
        self.assertEqual(struct.unpack_from("<IIBBBBB", score, 16), (0, 2756, 48, 128, 32, 1, 255))
        self.assertEqual(struct.unpack_from("<IIBBBBB", score, 29), (2205, 2756, 60, 128, 0, 0, 0))

    def test_checked_in_score(self):
        score = (ROOT / "launcher/music/cantina-band.score").read_bytes()
        magic, frames, count = struct.unpack_from("<8sII", score)
        self.assertEqual((magic, count), (b"OJCHIP01", 8844))
        self.assertEqual(len(score), 16 + count * 13)
        self.assertLess(len(score), 120000)
        self.assertTrue(159 < frames / 22050 < 161)
        self.assertFalse((ROOT / "launcher/music/cantina-band.wav").exists())
        self.assertFalse(tuple((ROOT / "launcher").rglob("*.mid")))


if __name__ == "__main__":
    unittest.main()
