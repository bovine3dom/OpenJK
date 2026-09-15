#!/usr/bin/env python3
"""Check asset selection and conversion with synthetic game archives."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("import_jo", Path(__file__).with_name("import-jo.py"))
assert spec is not None and spec.loader is not None
jo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(jo)


def script(value):
    return (b"IBI\0" + struct.pack("<fiiBii", 1.57, 26, 1, 0, 4, len(value)) + value)


class ImportTests(unittest.TestCase):
    def test_script_rewrite_preserves_blocks_and_members(self):
        source = script(b"BOTH_COCKPIT_SIT\0")
        result = jo.convert_script(source, {b"BOTH_COCKPIT_SIT\0": b"BOTH_CIN_1\0"})
        self.assertEqual(result, script(b"BOTH_CIN_1\0"))
        self.assertEqual(jo.convert_script(source, {}), source)
        with self.assertRaises((ValueError, struct.error)):
            jo.convert_script(source[:-1], {})
        with self.assertRaises(ValueError):
            jo.convert_script(b"bad version", {})

    def test_overlay_keeps_ja_gameplay_and_jo_cinematic_skeletons(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            ja, source = root / "JA", root / "JO"
            for directory in (ja, source):
                (directory / "base").mkdir(parents=True)
            human = "models/players/_humanoid/"
            base_strings = b'REFERENCE EXISTING\nLANG_ENGLISH "Keep this JA label"\n'
            with zipfile.ZipFile(ja / "base/assets0.pk3", "w") as archive:
                archive.writestr(human + "animation.cfg", b"BOTH_STAND1 10 2 0 20\n")
                archive.writestr("strings/english/sp_ingame.str", base_strings)
                archive.writestr("ui/newgame.menu", b"open characterMenu ;")
            entities = ('{\n"classname" "NPC_Kyle"\n"NPC_targetname" "cinematic1_kyle"\n}\n').encode()
            bsp = b"RBSP" + struct.pack("<iii", 1, 16, len(entities) + 1) + entities + b"\0"
            text = b'INDEX 0\n{\n REFERENCE KEJIM_POST_OBJ1\n TEXT_LANGUAGE1 "Investigate."\n}\n'
            with zipfile.ZipFile(source / "base/assets0.pk3", "w") as archive:
                for mapname in ("kejim_post", "kejim_base"):
                    archive.writestr(f"maps/{mapname}.bsp", bsp)
                archive.writestr("ext_data/npcs.cfg", b"Kyle\n{\nplayerModel kyle\n}\nJan\n{\nplayerModel jan\n}\n")
                archive.writestr(human + "animation.cfg", b"BOTH_COCKPIT_SIT 30 5 0 20\n")
                archive.writestr(human + "_humanoid.gla", bytes(100))
                for actor in ("kyle", "jan"):
                    archive.writestr(f"models/players/{actor}/model.glm", bytes(164))
                    archive.writestr(f"models/players/{actor}/model_default.skin", b"torso,texture")
                archive.writestr("scripts/cinematics/cinematic1.ibi", script(b"BOTH_COCKPIT_SIT\0"))
                archive.writestr("strip/objectives.sp", text)
                archive.writestr("strip/sp_ingame.sp", text)
                archive.writestr("ui/main.menu", b"DO NOT IMPORT")
                archive.writestr("ext_data/weapons.dat", b"DO NOT IMPORT")
                archive.writestr("models/weapons2/blaster/model.glm", b"DO NOT IMPORT")
                archive.writestr("textures/kejim/wall.tga", b"original")
            with zipfile.ZipFile(source / "base/assets2.pk3", "w") as archive:
                archive.writestr("textures/kejim/wall.tga", b"patched")
            original = {p: p.read_bytes() for p in root.glob("*/base/*.pk3")}
            overlay = root / "overlay.pk3"
            jo.build_overlay(ja, source, overlay)
            with zipfile.ZipFile(overlay) as archive:
                names = archive.namelist()
                self.assertEqual(len(names), len(set(names)))
                self.assertNotIn("ui/main.menu", names)
                self.assertNotIn("ext_data/weapons.dat", names)
                self.assertNotIn("models/weapons2/blaster/model.glm", names)
                self.assertEqual(archive.read("textures/kejim/wall.tga"), b"patched")
                self.assertEqual(archive.read(human + "animation.cfg"), b"BOTH_STAND1 10 2 0 20\n")
                self.assertNotIn(human + "_humanoid.gla", names)
                self.assertEqual(archive.read("models/players/jo_cinematic/animation.cfg"), b"BOTH_CIN_1 30 5 0 20\n")
                glm = archive.read("models/players/jo_cinematic_kyle/model.glm")
                gla = archive.read("models/players/jo_cinematic/jo_cinematic.gla")
                self.assertEqual(glm[72:136].rstrip(b"\0") + b".gla", gla[8:72].rstrip(b"\0"))
                self.assertIn(b'"NPC_type" "jo_cinematic_kyle"', archive.read("maps/kejim_post.ent"))
                self.assertIn(b"playerModel jo_cinematic_kyle", archive.read("ext_data/jo/npcs.cfg"))
                self.assertEqual(archive.read("scripts/cinematics/cinematic1.ibi"), script(b"BOTH_CIN_1\0"))
                self.assertIn(b"Keep this JA label", archive.read("strings/english/sp_ingame.str"))
                self.assertEqual(archive.read("ext_data/jo/objectives.dat"), b"KEJIM_POST_OBJ1\n")
                self.assertEqual(archive.read("ui/newgame.menu"), b"uiScript startgame ;")
            for path, data in original.items():
                self.assertEqual(path.read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
