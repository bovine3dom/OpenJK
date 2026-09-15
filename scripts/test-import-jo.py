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
    def test_prisoner_alternate_head_surfaces_remain_distinct(self):
        names = (b"head", b"head_off", b"head_face", b"head_face_off", b"head_cap_torso_off")
        model = bytearray(164)
        struct.pack_into("<ii", model, 152, len(names), 164)
        for name in names:
            model.extend(name.ljust(64, b"\0") + struct.pack("<I", 2 if name.endswith(b"_off") else 0)
                         + bytes(76))
        converted = jo.convert_model(model)
        actual = [converted[164 + i * 144:228 + i * 144].rstrip(b"\0") for i in range(len(names))]
        self.assertEqual(actual, [b"head", b"head_alt", b"head_face", b"head_face_alt", b"head_cap_torso_off"])
        for i in range(len(names)):
            self.assertEqual(converted[228 + i * 144:308 + i * 144], model[228 + i * 144:308 + i * 144])
        skin = b"head,head_01\nhead_off,head_02\nhead_cap_torso_off,caps\n"
        self.assertEqual(jo.convert_skin(skin), b"head,head_01\nhead_alt,head_02\nhead_cap_torso_off,caps\n")
        npc = 'Prisoner2\n{\nsurfOff "head head_face"\nsurfOn "head_off head_face_off"\n}\n'
        self.assertIn('surfOn "head_alt head_face_alt"', jo.convert_npcs(npc))
        self.assertIn('surfOff "head head_face"', jo.convert_npcs(npc))

    def test_shader_parser_handles_comments_and_nested_stages(self):
        text = b'// ignored {\n"gfx/example" { /* } */ { map "a{b}.tga" } }\nworld/test { { map rock } }'
        shaders = dict(jo.shader_definitions(text))
        self.assertEqual(set(shaders), {"gfx/example", "world/test"})
        self.assertIn(b'"a{b}.tga"', shaders["gfx/example"])
        for invalid in (b"missing_body", b"unclosed { { map rock }"):
            with self.assertRaises(ValueError):
                list(jo.shader_definitions(invalid))

    def test_npc_classes_use_academy_names(self):
        source = ('StormTrooper\n{\n class stormtrooper\n playerTeam enemy\n}\n'
                  'Galak\n{\n CLASS "galak_mech"\n}\nJan\n{\n class CLASS_JAN\n}\nMorganKatarn\n{\n class morgan\n}\n')
        converted = jo.convert_npcs(source)
        self.assertIn("class CLASS_STORMTROOPER", converted)
        self.assertIn("CLASS CLASS_GALAKMECH", converted)
        self.assertIn("class CLASS_JAN", converted)
        self.assertIn("class CLASS_MORGANKATARN", converted)
        self.assertIn("playerTeam enemy", converted)
        self.assertEqual(jo.convert_npcs(converted), converted)

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
                archive.writestr("shaders/ui.shader", b"gfx/menus/scanlines { { map scan blendFunc add } }\ngfx/hud/vehicle_frame { { map frame blendFunc blend } }")
                archive.writestr("shaders/desert.shader", b"textures/kejim/panel { { map wrong } }")
                archive.writestr("gfx/menus/scanlines.tga", b"JA image")
            entities = ('{\n"classname" "NPC_Kyle"\n"NPC_targetname" "cinematic1_kyle"\n}\n').encode()
            bsp = b"RBSP" + struct.pack("<iii", 1, 16, len(entities) + 1) + entities + b"\0"
            text = b'INDEX 0\n{\n REFERENCE KEJIM_POST_OBJ1\n TEXT_LANGUAGE1 "Investigate."\n}\n'
            presentation = {
                "ext_data/dms.dat": b"levelmusic { kejim_post { explore ImpBaseB_Explore } }",
                "music/kejim_post/impbaseb_explore.mp3": b"music samples",
                "levelshots/kejim_post.jpg": b"level preview",
                "levelshots/kejim_base.jpg": b"base preview",
                "menu/new/title.tga": b"console title",
                "menu/art/unknownmap.jpg": b"loading artwork",
            }
            with zipfile.ZipFile(source / "base/assets0.pk3", "w") as archive:
                for name, data in presentation.items():
                    archive.writestr(name, data)
                for mapname in ("kejim_post", "kejim_base"):
                    archive.writestr(f"maps/{mapname}.bsp", bsp)
                archive.writestr("ext_data/npcs.cfg", b"Kyle\n{\nplayerModel kyle\nclass kyle\n}\nJan\n{\nplayerModel jan\nclass jan\n}\nGalak\n{\nplayerModel galak\nclass galak\n}\n")
                archive.writestr(human + "animation.cfg", b"BOTH_COCKPIT_SIT 30 5 0 20\nBOTH_TALKGESTURE11START 50 33 -1 20\nBOTH_TALKGESTURE11STOP 83 16 -1 20\nBOTH_TALKGESTURE2 99 39 -1 20\n")
                archive.writestr(human + "_humanoid.gla", bytes(100))
                for actor in ("kyle", "jan", "galak"):
                    archive.writestr(f"models/players/{actor}/model.glm", bytes(164))
                    archive.writestr(f"models/players/{actor}/model_default.skin", b"torso,texture")
                archive.writestr("scripts/cinematics/cinematic1.ibi", script(b"BOTH_COCKPIT_SIT\0"))
                archive.writestr("scripts/cinematics/cinematic2.ibi", script(b"BOTH_TALKGESTURE2\0"))
                archive.writestr("models/players/galak_mech/animation.cfg",
                                  b"BOTH_ALERT1 10 20 -1 20\nBOTH_TRIUMPHANT1STOP 30 10 -1 20\n")
                archive.writestr("strip/objectives.sp", text)
                archive.writestr("strip/sp_ingame.sp", text)
                archive.writestr("ui/main.menu", b"DO NOT IMPORT")
                archive.writestr("ext_data/weapons.dat", b"DO NOT IMPORT")
                archive.writestr("models/weapons2/blaster/model.glm", b"DO NOT IMPORT")
                archive.writestr("textures/kejim/wall.tga", b"original")
                archive.writestr("shaders/ui.shader", b"console { { map menu/new/title } }")
                archive.writestr("shaders/imperial.shader", b"textures/kejim/panel { { map correct } }")
                archive.writestr("gfx/menus/scanlines.tga", b"JO image")
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
                for name, data in presentation.items():
                    self.assertEqual(archive.read(name), data)
                self.assertEqual(archive.read("textures/kejim/wall.tga"), b"patched")
                self.assertEqual(archive.read(human + "animation.cfg"), b"BOTH_STAND1 10 2 0 20\n")
                self.assertNotIn(human + "_humanoid.gla", names)
                self.assertEqual(archive.read("models/players/jo_cinematic/animation.cfg"),
                                 b"BOTH_CIN_1 30 5 0 20\nBOTH_CIN_2 50 33 -1 20\nBOTH_CIN_3 83 16 -1 20\nBOTH_CIN_4 99 39 -1 20\n")
                glm = archive.read("models/players/jo_cinematic_kyle/model.glm")
                gla = archive.read("models/players/jo_cinematic/jo_cinematic.gla")
                self.assertEqual(glm[72:136].rstrip(b"\0") + b".gla", gla[8:72].rstrip(b"\0"))
                self.assertIn(b'"NPC_type" "jo_cinematic_kyle"', archive.read("maps/kejim_post.ent"))
                self.assertIn(b"playerModel jo_cinematic_kyle", archive.read("ext_data/jo/npcs.cfg"))
                self.assertIn(b"class CLASS_KYLE", archive.read("ext_data/jo/npcs.cfg"))
                self.assertIn(b"playerModel jo_cinematic_galak", archive.read("ext_data/jo/npcs.cfg"))
                self.assertEqual(archive.read("scripts/cinematics/cinematic1.ibi"), script(b"BOTH_CIN_1\0"))
                self.assertEqual(archive.read("scripts/cinematics/cinematic2.ibi"), script(b"BOTH_CIN_4\0"))
                self.assertEqual(archive.read("models/players/galak_mech/animation.cfg"),
                                 b"BOTH_CIN_45 10 20 -1 20\nBOTH_CIN_50 30 10 -1 20\n")
                self.assertIn(b"Keep this JA label", archive.read("strings/english/sp_ingame.str"))
                self.assertEqual(archive.read("ext_data/jo/objectives.dat"), b"KEJIM_POST_OBJ1\n")
                self.assertEqual(archive.read("ui/newgame.menu"), b"uiScript startgame ;")
                shaders = dict(jo.shader_definitions(archive.read("shaders/jo_campaign.shader")))
                self.assertIn(b"blendFunc add", shaders["gfx/menus/scanlines"])
                self.assertIn(b"blendFunc blend", shaders["gfx/hud/vehicle_frame"])
                self.assertIn(b"map correct", shaders["textures/kejim/panel"])
                self.assertIn(b"menu/new/title", shaders["console"])
                self.assertNotIn("gfx/menus/scanlines.tga", names)
                for path in ("shaders/ui.shader", "shaders/desert.shader", "shaders/imperial.shader"):
                    self.assertEqual(list(jo.shader_definitions(archive.read(path))), [])
            for path, data in original.items():
                self.assertEqual(path.read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
