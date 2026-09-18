#!/usr/bin/env python3
"""Check asset selection and conversion with synthetic game archives."""

import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import sys
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
    def test_kejim_door_patch_excludes_only_bridge_guard(self):
        guards = [f'{{"classname" "NPC_Stormtrooper" "NPC_target" "st_death" "origin" "{i} 0 32"}}' for i in range(6)]
        bridge = '{"classname" "NPC_Stormtrooper" "NPC_target" "st_death" "origin" "188 -252 360"}'
        door = '{"classname" "target_counter" "targetname" "st_death" "target" "run_check_door" "count" "7"}'
        fight = '{"classname" "target_counter" "targetname" "st_death" "Usescript" "kejim_post/jan_fight" "count" "2"}'
        recipe = json.loads(jo.PATCH_FILE.read_text())["kejim_post"]
        result = jo.patch_entities("\n".join([*guards, bridge, door, fight]).encode(), recipe).decode()
        entities = [dict(re.findall(r'"([^"]*)"\s*"([^"]*)"', block)) for block in re.findall(r'\{[^}]*\}', result)]
        self.assertEqual([e["NPC_target"] for e in entities[:6]], ["jo_ground_death"] * 6)
        self.assertEqual(entities[6]["NPC_target"], "st_death")
        self.assertEqual((entities[7]["targetname"], entities[7]["count"]), ("jo_ground_death", "6"))
        self.assertEqual((entities[8]["targetname"], entities[8]["count"]), ("st_death", "2"))
        self.assertEqual(entities[9]["classname"], "target_relay")
        self.assertEqual((entities[9]["targetname"], entities[9]["target"]), ("jo_ground_death", "st_death"))

    def test_entity_patch_checks_source_and_retains_quoted_braces(self):
        data = b'{"classname" "worldspawn" "message" "A {room}" "old" "1"}'
        edit = {"match": {"classname": "worldspawn"}, "expect": 2, "remove": ["old"], "set": {"music": "test"}}
        with self.assertRaisesRegex(ValueError, "expected 2 matches"):
            jo.patch_entities(data, {"edit": [edit]})
        edit["expect"] = 1
        result = jo.patch_entities(data, {"edit": [edit]})
        self.assertIn(b'"message" "A {room}"', result)
        self.assertIn(b'"music" "test"', result)
        self.assertNotIn(b'"old"', result)

    def test_legacy_jedi_have_sabers_and_force_data(self):
        source = 'Desann\n{\nclass desann\nsaberColor red\n}\nKyle\n{\nclass kyle\nsaberColor blue\n}\n'
        converted = jo.convert_npcs(source)
        desann, kyle = converted.split("Kyle\n", 1)
        self.assertIn("saber Desann", desann)
        self.assertIn("FP_PUSH 3", desann)
        self.assertIn("FP_LIGHTNING 3", desann)
        self.assertLess(desann.index("saber Desann"), desann.index("saberColor red"))
        self.assertIn("saber Kyle", kyle)
        self.assertNotIn("FP_", kyle)
        self.assertEqual(jo.convert_npcs(converted), converted)

    def test_explicit_force_and_saber_settings_survive(self):
        converted = jo.convert_npcs('custom\n{\nclass desann\nsaber custom_saber\nsaberColor blue\nFP_PUSH 0\n}\n')
        self.assertIn("saber custom_saber", converted)
        self.assertNotIn("saber Desann", converted)
        self.assertIn("FP_PUSH 0", converted)
        self.assertNotIn("FP_PUSH 3", converted)
        self.assertIn("FP_LIGHTNING 3", converted)

    def test_strings_allow_metadata_before_english_text(self):
        data = (b'REFERENCE INGAME\nCOUNT 2\nINDEX 0\n{\n REFERENCE SECRETAREAS_OF\n'
                b' NOTES "used for "0 of 3" secrets"\n TEXT_LANGUAGE1 "of"\n}\n'
                b'INDEX 1\n{\n REFERENCE TITLE\n TEXT_LANGUAGE1 "Level stats"\n}\n')
        self.assertEqual(jo.strings(data), [("SECRETAREAS_OF", "of"), ("TITLE", "Level stats")])

    def test_alternate_character_surfaces_remain_distinct(self):
        names = (b"head", b"head_off", b"head_face", b"head_face_off", b"head_cap_torso_off", b"torso_augment_off", b"head_fins_off")
        model = bytearray(164)
        struct.pack_into("<ii", model, 152, len(names), 164)
        for name in names:
            model.extend(name.ljust(64, b"\0") + struct.pack("<I", 2 if name.endswith(b"_off") else 0)
                         + bytes(76))
        converted = jo.convert_model(model)
        actual = [converted[164 + i * 144:228 + i * 144].rstrip(b"\0") for i in range(len(names))]
        self.assertEqual(actual, [b"head", b"head_alt", b"head_face", b"head_face_alt", b"head_cap_torso_off", b"torso_augment_alt", b"head_fins_alt"])
        for i in range(len(names)):
            self.assertEqual(converted[228 + i * 144:308 + i * 144], model[228 + i * 144:308 + i * 144])
        skin = b"head,head_01\nhead_off,head_02\nhead_cap_torso_off,caps\n"
        self.assertEqual(jo.convert_skin(skin), b"head,head_01\nhead_alt,head_02\nhead_cap_torso_off,caps\n")
        npc = 'Prisoner2\n{\nsurfOff "head head_face"\nsurfOn "head_off head_face_off"\n}\n'
        self.assertIn('surfOn "head_alt head_face_alt"', jo.convert_npcs(npc))
        self.assertIn('surfOff "head head_face"', jo.convert_npcs(npc))
        self.assertIn('surfOn "torso_augment_alt"', jo.convert_npcs('Rodian2\n{\nsurfOn "torso_augment_off"\n}\n'))
        self.assertEqual(jo.convert_skin(b"torso_augment_off,back\nhead_fins_off,fins\n"), b"torso_augment_alt,back\nhead_fins_alt,fins\n")

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
                archive.writestr("maps/yavin1.bsp", b"synthetic map")
                archive.writestr("strings/english/sp_ingame.str", base_strings)
                archive.writestr("ui/newgame.menu", b"open characterMenu ;")
                archive.writestr("shaders/ui.shader", b"gfx/menus/scanlines { { map scan blendFunc add } }\ngfx/hud/vehicle_frame { { map frame blendFunc blend } }")
                archive.writestr("shaders/desert.shader", b"textures/kejim/panel { { map wrong } }")
                archive.writestr("gfx/menus/scanlines.tga", b"JA image")
            entities = ['{\n"classname" "NPC_Kyle"\n"NPC_targetname" "cinematic1_kyle"\n}']
            entities.extend(f'{{"classname" "NPC_Stormtrooper" "NPC_target" "st_death" "origin" "{i} 0 32"}}'
                            for i in range(6))
            entities.extend((
                '{"classname" "NPC_Stormtrooper" "NPC_target" "st_death" "origin" "188 -252 360"}',
                '{"classname" "target_counter" "targetname" "st_death" "target" "run_check_door" "count" "7"}',
                '{"classname" "target_counter" "targetname" "st_death" "Usescript" "kejim_post/jan_fight" "count" "2"}',
            ))
            entities = "\n".join(entities).encode()
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
                archive.writestr("ext_data/npcs.cfg", b"Kyle\n{\nplayerModel kyle\nclass kyle\n}\nJan\n{\nplayerModel jan\nclass jan\n}\nGalak\n{\nplayerModel galak\nclass galak\n}\nTavion\n{\nplayerModel tavion\nclass tavion\ncustomSkin red\n}\n")
                archive.writestr(human + "animation.cfg", b"BOTH_COCKPIT_SIT 30 5 0 20\nBOTH_TALKGESTURE11START 50 33 -1 20\nBOTH_TALKGESTURE11STOP 83 16 -1 20\nBOTH_TALKGESTURE2 99 39 -1 20\n")
                archive.writestr(human + "_humanoid.gla", bytes(100))
                for actor in ("kyle", "jan", "galak", "tavion"):
                    mesh = bytearray(164)
                    mesh[72:136] = b"models/players/_humanoid/_humanoid".ljust(64, b"\0")
                    archive.writestr(f"models/players/{actor}/model.glm", mesh)
                    archive.writestr(f"models/players/{actor}/model_default.skin", b"torso,texture")
                archive.writestr("models/players/tavion/model_red.skin", b"torso,red_texture")
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
                archive.writestr("shaders/jo_campaign.shader", b"textures/kejim/generated { { map merged } }")
                archive.writestr("gfx/menus/scanlines.tga", b"JO image")
            with zipfile.ZipFile(source / "base/assets2.pk3", "w") as archive:
                archive.writestr("textures/kejim/wall.tga", b"patched")
            for directory, numbers in ((ja, (1, 2, 3)), (source, (1, 5))):
                for number in numbers:
                    with zipfile.ZipFile(directory / f"base/assets{number}.pk3", "w"):
                        pass
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
                                 b"BOTH_COCKPIT_SIT 30 5 0 20\nBOTH_TALKGESTURE11START 50 33 -1 20\nBOTH_TALKGESTURE11STOP 83 16 -1 20\nBOTH_TALKGESTURE2 99 39 -1 20\n"
                                 b"BOTH_CIN_1 30 5 0 20\nBOTH_CIN_2 50 33 -1 20\nBOTH_CIN_3 83 16 -1 20\nBOTH_CIN_4 99 39 -1 20\n")
                glm = archive.read("models/players/jo_cinematic_kyle/model.glm")
                gla = archive.read("models/players/jo_cinematic/jo_cinematic.gla")
                self.assertEqual(glm[72:136].rstrip(b"\0") + b".gla", gla[8:72].rstrip(b"\0"))
                patched_entities = archive.read("maps/kejim_post.ent")
                self.assertEqual(patched_entities.count(b'"NPC_target" "jo_ground_death"'), 6)
                self.assertIn(b'"targetname" "jo_ground_death"', patched_entities)
                self.assertEqual(archive.read("maps/kejim_post.bsp"), bsp)
                self.assertNotIn("maps/kejim_base.ent", names)
                self.assertIn(b"playerModel jo_cinematic_kyle", archive.read("ext_data/jo/npcs.cfg"))
                self.assertIn(b"class CLASS_KYLE", archive.read("ext_data/jo/npcs.cfg"))
                self.assertIn(b"playerModel jo_cinematic_galak", archive.read("ext_data/jo/npcs.cfg"))
                self.assertIn(b"\njo_cinematic_tavion\n", archive.read("ext_data/jo/npcs.cfg"))
                self.assertEqual(archive.read("models/players/jo_cinematic_tavion/model_red.skin"), b"torso,red_texture")
                self.assertEqual(archive.read("scripts/cinematics/cinematic1.ibi"), script(b"BOTH_CIN_1\0"))
                self.assertEqual(archive.read("scripts/cinematics/cinematic2.ibi"), script(b"BOTH_CIN_4\0"))
                self.assertEqual(archive.read("models/players/galak_mech/animation.cfg"),
                                 b"BOTH_ALERT1 10 20 -1 20\nBOTH_TRIUMPHANT1STOP 30 10 -1 20\n"
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
            native_importer = os.environ.get("OPENJK_IMPORT_JO")
            if native_importer:
                profile = root / "profile"
                subprocess.run((native_importer, ja, source, profile), check=True, text=True, capture_output=True)
                with zipfile.ZipFile(overlay) as expected, zipfile.ZipFile(profile / "OpenJK/zz_jo_campaign.pk3") as actual:
                    self.assertEqual(actual.namelist(), expected.namelist())
                    for name in expected.namelist():
                        self.assertEqual(actual.read(name), expected.read(name), name)
                cached = subprocess.run((native_importer, ja, source, profile), check=True, text=True, capture_output=True)
                self.assertIn("Using cached archive", cached.stdout)
                target = profile / "OpenJK/zz_jo_campaign.pk3"
                imported = target.read_bytes()
                damaged = source / "base/assets2.pk3"
                damaged.write_bytes(b"not a PK3")
                failed = subprocess.run((native_importer, ja, source, profile), text=True, capture_output=True)
                self.assertNotEqual(failed.returncode, 0)
                self.assertEqual(target.read_bytes(), imported)
                damaged.write_bytes(original[damaged])
            launcher = os.environ.get("OPENJK_LAUNCHER")
            if launcher:
                archive_path = ja / "base/assets0.pk3"
                damaged_archive = bytearray(archive_path.read_bytes())
                damaged_archive[:4] = b"BAD!"
                archive_path.write_bytes(damaged_archive)
                damaged_check = subprocess.run((launcher, "--headless-check", "--ja-path", ja),
                    text=True, capture_output=True)
                self.assertNotEqual(damaged_check.returncode, 0)
                self.assertIn("damaged_archive", damaged_check.stdout)
                archive_path.write_bytes(original[archive_path])
                overlap = subprocess.run((launcher, "--print-launch", "--ja-path", ja,
                    "--profile", ja / "profile", "--engine", sys.executable), text=True, capture_output=True)
                self.assertNotEqual(overlap.returncode, 0)
                self.assertIn("outside", overlap.stderr)
                linked_profile = root / "linked-profile"
                linked_profile.mkdir()
                try:
                    (linked_profile / "OpenJK").symlink_to(ja, target_is_directory=True)
                except OSError:
                    pass
                else:
                    linked = subprocess.run((launcher, "--print-launch", "--ja-path", ja,
                        "--profile", linked_profile, "--engine", sys.executable), text=True, capture_output=True)
                    self.assertNotEqual(linked.returncode, 0)
                    self.assertIn("outside", linked.stderr)
                alias_profile = root / "alias-profile"
                shared_output = alias_profile / "shared"
                shared_output.mkdir(parents=True)
                try:
                    (alias_profile / "OpenJK").symlink_to(shared_output, target_is_directory=True)
                    (alias_profile / "campaigns/jo").mkdir(parents=True)
                    (alias_profile / "campaigns/jo/OpenJK").symlink_to(shared_output, target_is_directory=True)
                except OSError:
                    pass
                else:
                    alias = subprocess.run((launcher, "--print-launch", "--ja-path", ja, "--jo-path", source,
                        "--profile", alias_profile, "--engine", sys.executable, "--campaign", "jo"),
                        text=True, capture_output=True)
                    self.assertNotEqual(alias.returncode, 0)
                    self.assertIn("separate", alias.stderr)
                launcher_profile = root / "profile with = \N{LATIN SMALL LETTER E WITH ACUTE}"
                printed = subprocess.run((launcher, "--print-launch", "--ja-path", ja / "base",
                    "--jo-path", source, "--profile", launcher_profile, "--engine", sys.executable,
                    "--campaign", "ja", "--new-game", "--", "+set", "quoted value"),
                    check=True, text=True, capture_output=True)
                arguments = [json.loads(line) for line in printed.stdout.splitlines()]
                expected = [str(Path(sys.executable).absolute()), "+set", "fs_basepath", str(Path(launcher).resolve().parent),
                    "+set", "fs_cdpath", str(ja.resolve()), "+set", "fs_homepath", str(launcher_profile),
                    "+set", "fs_game", "OpenJK", "+set", "com_outcast", "0",
                    "+set", "r_mode", "-2", "+set", "r_fullscreen", "1", "+set", "cg_fovAspectAdjust", "1", "+map", "yavin1",
                    "+set", "quoted value"]
                self.assertEqual(arguments, expected)
                checked = subprocess.run((launcher, "--headless-check", "--profile", launcher_profile),
                    check=True, text=True, capture_output=True)
                self.assertIn("JA: ready", checked.stdout)
                self.assertIn("JO: ready", checked.stdout)
                printed = subprocess.run((launcher, "--print-launch", "--profile", launcher_profile,
                    "--engine", sys.executable, "--campaign", "jo", "--new-game"),
                    check=True, text=True, capture_output=True)
                arguments = [json.loads(line) for line in printed.stdout.splitlines()]
                expected = [str(Path(sys.executable).absolute()), "+set", "fs_basepath", str(Path(launcher).resolve().parent),
                    "+set", "fs_cdpath", str(ja.resolve()), "+set", "fs_homepath",
                    str(launcher_profile / "campaigns/jo"), "+set", "fs_game", "OpenJK", "+set",
                    "com_outcast", "1", "+set", "r_mode", "-2", "+set", "r_fullscreen", "1",
                    "+set", "cg_fovAspectAdjust", "1", "+map", "kejim_post"]
                self.assertEqual(arguments, expected)
                def resume(campaign):
                    return subprocess.run((launcher, "--print-launch", "--profile", launcher_profile,
                        "--engine", sys.executable, "--campaign", campaign, "--continue"),
                        text=True, capture_output=True)

                self.assertNotEqual(resume("ja").returncode, 0)
                saves = launcher_profile / "OpenJK/saves"
                saves.mkdir(parents=True)
                for index, name in enumerate(("older", "auto", "current", "bad;quit")):
                    save = saves / (name + ".sav")
                    save.write_bytes(b"fixture")
                    os.utime(save, (100 + index, 100 + index))
                (saves / "empty.sav").touch()
                continued = resume("ja")
                self.assertEqual(continued.returncode, 0, continued.stderr)
                self.assertEqual([json.loads(line) for line in continued.stdout.splitlines()][-2:], ["+load", "auto"])
                self.assertNotEqual(resume("jo").returncode, 0)
                (launcher_profile / "OpenJK/openjk_sp.cfg").write_text("seta r_mode 4\n")
                self.assertNotIn('"r_mode"', resume("ja").stdout)
                with zipfile.ZipFile(overlay) as expected, zipfile.ZipFile(
                        launcher_profile / "campaigns/jo/OpenJK/zz_jo_campaign.pk3") as actual:
                    self.assertEqual(actual.namelist(), expected.namelist())
                    for name in expected.namelist():
                        self.assertEqual(actual.read(name), expected.read(name), name)
            for path, data in original.items():
                self.assertEqual(path.read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
