#!/usr/bin/env python3
"""Check cinematic reference parsing with nested ICARUS scopes."""

import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("audit", Path(__file__).with_name("audit-jo-cinematics.py"))
assert spec and spec.loader
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def block(op, *values):
    result = struct.pack("<iiB", op, len(values), 0)
    for value in values:
        data = value.encode() + b"\0"
        result += struct.pack("<ii", 4, len(data)) + data
    return result


class CinematicAuditTests(unittest.TestCase):
    def test_nested_tasks_keep_the_actor_and_restore_outer_scope(self):
        data = b"IBI\0" + struct.pack("<f", 1.57)
        data += block(19, "cinematic_jan", "56")
        data += block(41, "greeting") + block(26, "SET_ANIM_BOTH", "BOTH_SIT1") + block(25)
        data += block(26, "SET_ADDRHANDBOLT_MODEL", "models/prop.glm") + block(25)
        data += block(26, "SET_ANIM_UPPER", "BOTH_STAND1")
        refs = list(audit.references(data))
        self.assertEqual([r["actor"] for r in refs], ["cinematic_jan", "cinematic_jan", "self"])
        self.assertEqual([r["kind"] for r in refs], ["animation", "prop", "animation"])
        self.assertEqual(data[refs[1]["offset"]:refs[1]["offset"] + 4], struct.pack("<i", 26))

    def test_frame_parser_excludes_comments_and_keeps_reverse_playback(self):
        frames = audit.animations(b"/* BOTH_FAKE 1 2 0 20 */\n// BOTH_FAKE2 1 2 0 20\nBOTH_SIT1 10 20 -1 -20\n")
        self.assertEqual(frames, {"BOTH_SIT1": (10, 20, -1, -20)})


if __name__ == "__main__":
    unittest.main()
