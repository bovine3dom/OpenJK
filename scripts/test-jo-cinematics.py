#!/usr/bin/env python3
"""Check JO cinematic completion, original poses, and attached props headlessly."""

import argparse
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MAPS = {"cctv": "kejim_base", "artus": "artus_mine", "topside": "artus_topside",
        "office": "kejim_base", "bar": "ns_streets", "rescue": "doom_detention", "shrine": "valley", "trial": "yavin_trial", "boarding": "ns_starpad", "jan-door": "kejim_post"}
ACTORS = {"cctv": ("cinematic2_kyle", "cinematic_galak", "cinematic_officer4"), "artus": ("cinematic4_kyle",),
          "topside": ("cinematic9_tavion", "cinematic9_jan", "cinematic9_desann", "cinematic9_kyle"),
          "office": ("cinematic3_kyle", "cinematic3_jan", "cinematic3_mon_mothma"),
          "bar": ("cinematic15_kyle", "cinematic15_bartender"), "rescue": ("cinematic29_kyle", "cinematic29_jan"),
          "shrine": ("cinematic10_kyle", "cinematic10_morgan"), "trial": ("cinematic13_kyle", "cinematic13_luke"), "boarding": ("lando",), "jan-door": ("jan", "jo_test_bridge")}


MAPS["droid"] = "bespin_undercity"
ACTORS["droid"] = ("droid",)
MAPS["turret"] = "ns_starpad"
ACTORS["turret"] = ("lady_luck_gun",)
MAPS["saber"] = "bespin_undercity"
ACTORS["saber"] = ("kyle_tube", "reborn_shaft")
MAPS["skies"] = "kejim_post"
ACTORS["skies"] = ("sky_test",)


def run_case(package, case, renderer, saved):
    output = ROOT / "build/jo-cinematics"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=f"{case}.{renderer}.", dir=output))
    profile = run / "profile"
    if case == "skies":
        folder = profile / "campaigns/jo/OpenJK"
        folder.mkdir(parents=True)
        with zipfile.ZipFile(folder / "zzz_old_skies.pk3", "w") as archive:
            for name in ("ns_streets", "ns_hideout", "ns_starpad"):
                archive.writestr(f"maps/{name}.atmosphere", (package / "OpenJK/maps/shared/jo-bespin.atmosphere").read_bytes())
    if case == "jan-door" and not saved:
        folder = profile / "campaigns/jo/OpenJK"
        subprocess.run([sys.executable, str(package / "import-jo.py"),
                        os.environ.get("OJK_ASSETS", str(ROOT / "GameData")),
                        os.environ.get("OJK_JO_ASSETS", str(ROOT / "GameData_JO")), str(folder.parent)], check=True)
        with zipfile.ZipFile(folder / "zz_jo_campaign.pk3") as archive:
            entities = archive.read("maps/kejim_post.ent").decode()
        # Give only the bridge guard a unique test name. Keep all death targets intact.
        entities, count = re.subn(r'\{[^}]*"origin" "188 -252 360"[^}]*\}',
            lambda m: m[0].replace('"NPC_targetname" "outside"', '"NPC_targetname" "jo_test_bridge"'), entities)
        assert count == 1
        entities, count = re.subn(r'\{[^}]*"origin" "-532 -236 32"[^}]*\}',
            lambda m: m[0].replace('"NPC_targetname" "outside"', '"NPC_targetname" "jo_test_last_ground"'), entities)
        assert count == 1
        with zipfile.ZipFile(folder / "zz_test_jan.pk3", "w") as archive:
            archive.writestr("maps/kejim_post.ent", entities)
            # Isolate the courtyard encounter from the introductory crouched approach.
            for name, settings in {
                "jan_courtyard": (("SET_ORIGIN", "-248 -136 32"), ("SET_CROUCHED", "false")),
                "keep_guard": (("SET_INVINCIBLE", "true"),),
            }.items():
                script = b"IBI\0" + struct.pack("<f", 1.57)
                for key, value in settings:
                    script += struct.pack("<iiB", 26, 2, 0)
                    for text in (key, value):
                        member = text.encode() + b"\0"
                        script += struct.pack("<ii", 4, len(member)) + member
                archive.writestr(f"scripts/tests/{name}.ibi", script)
    if case == "boarding" and not saved:
        folder = profile / "campaigns/jo/OpenJK"
        folder.mkdir(parents=True)
        with zipfile.ZipFile(Path(os.environ.get("OJK_JO_ASSETS", ROOT / "GameData_JO")) / "base/assets0.pk3") as archive:
            data = archive.read("maps/ns_starpad.bsp")
        start, size = struct.unpack_from("<ii", data, 8)
        entities = data[start:start + size].rstrip(b"\0").decode()
        # Start at the ramp. Keep the retail route, geometry, and boarding scripts.
        entities, count = re.subn(r'\{[^}]*"targetname" "lando_fuelship"[^}]*\}',
            lambda m: re.sub(r'"origin" "[^"]*"', '"origin" "128 920 -736"', m[0]), entities)
        assert count == 1
        with zipfile.ZipFile(folder / "zzz_boarding_test.pk3", "w") as archive:
            archive.writestr("maps/ns_starpad.ent", entities)
    if saved:
        saves = profile / "campaigns/jo/OpenJK/saves"
        saves.mkdir(parents=True)
        shutil.copyfile(saved, saves / "cinematic_test.sav")
    log = run / "console.log"
    mapname = MAPS[case]
    env = dict(os.environ, OJK_PROFILE=str(profile), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1",
               SDL_AUDIODRIVER="dummy", OJK_JO_ASSETS=os.environ.get("OJK_JO_ASSETS", str(ROOT / "GameData_JO")))
    command = ["bash", str(package / "launch-sp.sh"), os.environ.get("OJK_ASSETS", str(ROOT / "GameData")),
               "--campaign", "jo", "+safe", "+set", "cl_renderer", renderer,
               "+set", "r_fullscreen", "0", "+set", "r_mode", "3", "+set", "s_initsound", "1",
                "+set", "g_subtitles", "2", "+set", "developer", "1", "+set", "logfile", "2",
                "+set", "d_cinematicAnimations", "1", "+set", "con_notifytime", "-1",
               "+set", "com_maxfps", "20",
               *(["+load", "cinematic_test"] if saved else ["+map", mapname]),
               "+wait", "30", "+echo", "CINEMATIC_READY"]
    print(f"Cinematic results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT,
                                   env=env, text=True)
        assert process.stdin is not None
        stdin = process.stdin
        serial = 0

        def wait_for(marker, start=0):
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")[start:]
                if "ERROR:" in text:
                    raise RuntimeError(f"Game error: {log}")
                if marker in text:
                    return re.sub(r"\^[0-9]", "", text)
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited ({process.returncode}): {log}")
                time.sleep(0.03)
            raise TimeoutError(f"Missing {marker}: {log}")

        def cmd(text):
            nonlocal serial
            serial += 1
            marker = f"CINEMATIC_COMMAND_{serial}_DONE"
            start = len(log.read_text(errors="replace"))
            stdin.write(f"{text}; wait 1; echo {marker}\n")
            stdin.flush()
            return wait_for(marker, start)

        def samples(text):
            return [dict(word.split("=", 1) for word in line.split("cinematic ", 1)[1].split())
                    for line in text.splitlines() if "cinematic name=" in line]

        def capture(name):
            cmd(f"screenshot_png {name}")
            image = profile / "campaigns/jo/OpenJK/screenshots" / f"{name}.png"
            deadline = time.monotonic() + 30
            while not (image.is_file() and image.read_bytes().endswith(b"IEND\xaeB`\x82")):
                if time.monotonic() > deadline:
                    raise TimeoutError(f"Incomplete screenshot: {image}")
                time.sleep(0.05)
            subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
                            "-frames:v", "1", "-f", "null", "-"], check=True)

        try:
            wait_for("CINEMATIC_READY")
            if case == "cctv":
                cmd("helpusobi 1; use cinematic2_spawner")
            elif case == "topside":
                if not saved:
                    ready = cmd("helpusobi 1; god; wait 5")
                    assert "godmode ON" in ready, ready
                    cmd("wait 300; use tom_spawn; wait 20; god; wait 5; save before_topside; god; wait 5")
                if "camera=0" in cmd("campaign_status"):
                    cmd("helpusobi 1; use cinematic9_spawner")
            elif case == "office":
                cmd("helpusobi 1; wait 150; use cinematic3_script")
            elif case == "bar":
                cmd("helpusobi 1; wait 300; exitview; wait 100; use cinematic15_start")
            elif case == "rescue":
                cmd("helpusobi 1; wait 150; use jan_jail_door")
            elif case == "trial":
                cmd("helpusobi 1; wait 150; use cinematic13script")
            elif case == "skies":
                cmd("helpusobi 1; exitview; wait 200; god; noclip; setviewpos 100 160 600 180; cg_draw2D 0; wait 20")
                capture("kejim_dusk")
                assert "Atmosphere: kejim_post, sky=textures/skies/kejim" in log.read_text(errors="replace")
                for name in ("ns_streets", "ns_hideout", "ns_starpad"):
                    cmd(f"map {name}; wait 100; exitview; wait 100; r_atmosphereReload")
                    capture(name)
                    assert f"Atmosphere: {name}," not in log.read_text(errors="replace")
            elif case == "saber":
                cmd("helpusobi 1; god; saberColor yellow; use t179; wait 20; use cin_firstreborn")
            elif case == "turret":
                cmd("helpusobi 1; god; notarget; exitview; wait 100; use run_gun_down; wait 100; exitview; setviewpos 256 -808 -648 150; wait 20")
                capture("turret_external")
                cmd("use run_enter_reelo; wait 500; exitview; setviewpos 128 -1800 -700 270; wait 20")
                capture("reelo_turret")
                cmd("use lady_luck_gun; +attack; wait 10")
            elif case == "droid":
                if not saved:
                    cmd("helpusobi 1; god; notarget; setviewpos 4660 200 13880 270; use droiddoor; wait 100")
            elif case == "boarding":
                if not saved:
                    cmd("helpusobi 1; god; use hangardoors; wait 120")
                    cmd("setviewpos 220 460 -584 270; use run_lando_enter_ship")
            elif case == "jan-door":
                cmd("helpusobi 1; god; exitview; wait 200; use outside_troops; wait 20")
                cmd("runscript jo_test_last_ground tests/keep_guard; wait 5")
                cmd("runscript jan tests/jan_courtyard; wait 10")
                cmd("setviewpos -72 -800 80 90; npc kill st_alert1; npc kill st_alert2; wait 20")
                cmd("npc kill st_alert3; npc kill imp_alert; npc kill outside; wait 20")
                before = cmd("cinematic_status jan; nav doors tower_door")
                assert "origin=208.00,-472.00" not in before, "Jan checked the door before all ground guards died"
                assert re.search(r"navdoor .* name=tower_door .* closed=1", before), before
                cmd("npc kill jo_test_last_ground; wait 5")
            history = []
            captured = False
            seen_camera = False
            walking_frames = []
            poses_captured = set()
            query = f"wait {1 if case == 'artus' else 10}; " + "; ".join("cinematic_status " + actor for actor in ACTORS[case])
            if case in ("boarding", "droid"): query += "; campaign_status"
            if case == "jan-door": query += "; nav doors tower_door"
            deadline = time.monotonic() + (90 if case == "droid" else 300)
            while time.monotonic() < deadline:
                state = cmd(query)
                current = samples(state)
                if case == "cctv" and any(s["name"] == "cinematic_officer4" and s.get("legs") == "BOTH_WALK1"
                                          and s.get("nav") == "1" for s in current):
                    bone = re.search(r"cinematic_bone name=cinematic_officer4 frame=([\d.]+).* speed=([\d.]+)", state)
                    assert bone and float(bone[2]) > 0.1, "Officer walk animation is nearly frozen"
                    walking_frames.append(float(bone[1]))
                history.extend(current)
                if case == "saber" and any(s.get("name") == "kyle_tube" and s.get("absent") != "1" for s in current):
                    assert "cinematic_saber name=kyle_tube color=2" in state, state
                    if not captured and re.search(r"cinematic_combat name=kyle_tube .*blade_active=1 blade_length=[1-9]", state):
                        capture("yellow_saber")
                        captured = True
                if case == "droid" and "campaign=jo map=bespin_streets" in state:
                    break
                if case == "jan-door":
                    bridge = re.search(r"cinematic_combat name=jo_test_bridge health=(\d+)", state)
                    assert bridge and int(bridge[1]) > 0, "The bridge guard died before Jan finished"
                    if re.search(r"navdoor .* name=tower_door .* closed=0", state): break
                if case == "boarding" and "objective=NS_STARPAD_OBJ3 status=0" in state and "objective=NS_STARPAD_OBJ4 status=0" in state:
                    break
                if case == "topside":
                    for sample in current:
                        pose = sample.get("legs")
                        if sample["name"] == "cinematic9_kyle" and pose in ("BOTH_STAND5TOAIM", "BOTH_STAND5STARTLEDLOOKLEFT") and pose not in poses_captured:
                            boss = next(s for s in current if s["name"] == "cinematic9_desann")
                            dx, dy, _ = (b - k for b, k in zip(map(float, boss["origin"].split(",")), map(float, sample["origin"].split(","))))
                            facing = re.search(r"cinematic_combat name=cinematic9_kyle .* body_yaw=([-\d.]+)", state)
                            assert facing, state
                            delta = (float(facing[1]) - math.degrees(math.atan2(dy, dx)) + 180) % 360 - 180
                            assert abs(delta) < 60, f"Kyle faces away from Desann during {pose}: {delta} degrees"
                            capture(pose.lower())
                            poses_captured.add(pose)
                seen_camera |= any(s.get("camera") == "1" for s in current)
                if case in ("bar", "rescue", "shrine", "trial") and seen_camera and current and all(s.get("camera") == "0" for s in current):
                    break
                if case == "topside" and any(s.get("camera") == "0" and s.get("behavior") == "0" for s in current):
                    break
                if case != "saber" and not captured and any(s.get("legs", "").startswith("BOTH_CIN_") if case == "cctv"
                                        else s.get("legs") == "BOTH_EXAMINE2" if case == "office"
                                        else s.get("legs", "").startswith("BOTH_BARTENDER_") if case == "bar"
                                        else s.get("legs", "").startswith("BOTH_HUG") if case == "rescue"
                                        else s.get("nav") == "1" for s in current):
                    capture({"cctv": "galak", "office": "crystal", "bar": "bartender", "rescue": "hug"}.get(case, "walking"))
                    captured = True
                completed = current[:1] if case == "saber" else current[:2] if case == "cctv" else current
                if case != "droid" and completed and all(s.get("absent") == "1" for s in completed):
                    break
            else:
                if case == "droid":
                    cmd("save droid_stuck")
                raise TimeoutError(f"Cinematic did not complete: {log}")
            if case == "cctv":
                galak = [s for s in history if s["name"] == "cinematic_galak" and "absent" not in s]
                assert galak and any(s["voice"] == "1" for s in galak), "Galak never spoke"
                assert captured, "Missing JO gesture animation"
                assert walking_frames and max(walking_frames) - min(walking_frames) > 5, "Officer gait did not advance"
                following = samples(cmd("wait 10; cinematic_status cinematic3_mon_mothma"))
                assert following and "absent" not in following[0] and following[0]["camera"] == "1", following
            elif case == "artus":
                moving = [s for s in history if s.get("nav") == "1"
                          and math.hypot(*map(float, s["velocity"].split(",")[:2])) > 10]
                assert moving and all(s["noclip"] == "0" and s["legs"] == "BOTH_WALK1" for s in moving), moving
                assert any(s["ground"] != "1023" for s in moving), "Kyle never touched the ground"
                assert history[-1]["camera"] == "0", history[-1]
            elif case == "topside":
                desann = [s for s in history if s["name"] == "cinematic9_desann"]
                assert any(s.get("voice") == "1" for s in desann), "Desann never spoke after Tavion's handoff"
                assert any(s.get("camera") == "0" and s.get("behavior") == "0" for s in desann), "Desann fight did not start"
                assert any(s.get("legs") == "BOTH_CONSTRAINER1STAND" for s in history), "Missing Tavion restraint pose"
                assert any(s.get("legs") == "BOTH_CONSTRAINEE1STAND" for s in history), "Missing Jan restraint pose"
                assert len(poses_captured) == 2, "Missing Kyle aiming or startled pose"
                initial = cmd(("god; wait 5; " if not saved else "") + "campaign_status")
                player_health = re.search(r"health=(\d+)", initial)
                assert player_health, initial
                health = int(player_health[1])
                start = re.search(r"origin=([-\d.]+),([-\d.]+),([-\d.]+)", initial)
                assert start, initial
                arena = tuple(map(float, start.groups()))
                damaged = spent_force = False
                deadline = time.monotonic() + 180
                while time.monotonic() < deadline:
                    state = cmd("wait 10; cinematic_status cinematic9_desann; campaign_status")
                    combat = re.search(r"cinematic_combat name=cinematic9_desann ([^\n]+)", state)
                    if combat:
                        fields = dict(word.split("=", 1) for word in combat[1].split())
                        assert int(fields["blades"]) > 0, state
                        spent_force |= int(fields["active_force"]) != 0 or int(fields["force"]) < int(fields["max_force"])
                    player = re.search(r"campaign=jo map=artus_topside camera=(\d+) health=(\d+)", state)
                    if player:
                        damaged |= int(player[2]) < health
                        if player[1] == "1" and damaged: break
                        position = re.search(r"campaign=jo .* origin=([-\d.]+),([-\d.]+),([-\d.]+)", state)
                        if position and math.dist(arena[:2], tuple(map(float, position.groups()[:2]))) > 128:
                            # Keep the idle fixture in the arena after knockback; preserve health and enemy state.
                            cmd(f"setviewpos {arena[0]} {arena[1]} {arena[2] + 24} 135")
                else:
                    raise AssertionError("Desann did not defeat Kyle and start the aftermath")
                assert damaged and spent_force, "Desann did not perform a damaging Force attack"
                visible = set()
                sabers_captured = False
                deadline = time.monotonic() + 300
                while time.monotonic() < deadline:
                    state = cmd("wait 10; cinematic_status cinematic9_end_desann; cinematic_status cinematic9_tavion2; campaign_status")
                    for actor, data in re.findall(r"cinematic_combat name=(\S+) ([^\n]+)", state):
                        fields = dict(word.split("=", 1) for word in data.split())
                        if fields["weapon"] == "1" and int(fields["blade_active"]) and float(fields["blade_length"]) > 0 and int(fields["hilt"]) > 0:
                            visible.add(actor)
                    if len(visible) == 2 and not sabers_captured:
                        capture("aftermath_sabers")
                        sabers_captured = True
                    if "campaign=jo map=valley" in state: break
                else:
                    raise AssertionError("Artus aftermath did not reach the shrine map")
                assert visible == {"cinematic9_end_desann", "cinematic9_tavion2"}, visible
            elif case == "office":
                assert captured and any(s.get("legs") == "BOTH_SIT1" for s in history), "Missing office sitting or crystal pose"
                assert any(s["name"] == "cinematic3_jan" and s.get("legs", "").startswith("BOTH_CIN_") for s in history), "Jan did not sit in her chair"
                props = log.read_text(errors="replace")
                for actor in ("cinematic3_kyle", "cinematic3_mon_mothma"):
                    assert re.search(r"cinematic_prop name=" + actor + r" slot=\d+ model=models/map_objects/cinematics/crystal\.glm", props), (actor, "Missing crystal attachment")
            elif case == "bar":
                assert captured, "Missing bartender animations"
            elif case == "rescue":
                assert captured and "animation=BOTH_HUGGERSTOP2 supported=1" in log.read_text(errors="replace"), "Missing reunion poses"
                returned = cmd("wait 20; cinematic_status jan; campaign_status")
                assert "cinematic_set name=jan profile=_humanoid" in returned, returned
                assert "objective=DOOM_DETENTION_OBJ1 status=1" in returned, returned
            elif case in ("shrine", "trial"):
                assert seen_camera and captured, "Scene did not start or no movement task was observed"
                assert any(s.get("legs") == "BOTH_WALK1" and math.hypot(*map(float, s["velocity"].split(",")[:2])) > 10
                           for s in history), "No walking actor was observed"
                assert "camera=0" in cmd("campaign_status"), "Scene did not return control"
            elif case == "saber":
                assert captured and seen_camera, "Kyle's active yellow saber was not observed in the cutscene"
            elif case == "droid":
                assert any(s.get("nav") == "1" for s in history), "Droid did not navigate to the lift"
                assert all(s.get("noclip", "0") == "0" for s in history), "Droid bypassed collision"
                assert "campaign=jo map=bespin_streets" in cmd("campaign_status"), "Lift did not complete the level"
            elif case == "boarding":
                assert any(s.get("legs") == "BOTH_CONSOLE1" and s.get("voice") == "1" for s in history), "Lando did not give the roof and fuel instructions"
                assert all(s.get("noclip") == "0" for s in history), "Lando bypassed collision"
                assert float(history[-1]["origin"].split(",")[2]) > -530, "Lando did not reach the cockpit"
                cmd("setviewpos 150 110 -480 270; wait 20")
            elif case == "jan-door":
                jan = [s for s in history if s["name"] == "jan"]
                assert any(s.get("torso") == "BOTH_ATTACK3" for s in jan), "Jan did not shoot the door"
                assert any(s.get("voice") == "1" for s in jan), "Jan did not give her door dialogue"
                assert any(math.dist(tuple(map(float, s["origin"].split(","))), (208, -472, 32)) < 60 for s in jan), "Jan did not reach the door"
            capture("completed")
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            traces = [dict(word.split("=", 1) for word in line.split("cinematic_animation ", 1)[1].split())
                      for line in text.splitlines() if "cinematic_animation actor=" in line]
            staged = [s for s in traces if s["actor"].lower().startswith("cinematic")]
            assert (staged or case in ("boarding", "jan-door", "droid", "turret", "saber", "skies")) and all(s["supported"] == "1" for s in staged), [s for s in staged if s["supported"] != "1"]
            assert all(s["profile"] == "jo_cinematic" for s in staged), staged
            assert not re.search(r"ERROR:|Error:|Unknown command|[Cc]ouldn't open music file|trying to load fallback renderer", text), log
            print(f"PASS: JO {case} cinematic ({renderer})", flush=True)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            # Each run can regenerate this large archive; retain the logs, saves, and captures.
            (profile / "campaigns/jo/OpenJK/zz_jo_campaign.pk3").unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-vanilla")
    parser.add_argument("--case", choices=tuple(MAPS))
    parser.add_argument("--save", type=Path, help="Load a save from before the selected cinematic")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.save and not args.case:
        parser.error("--save requires --case")
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 640x480x24", sys.executable,
                                __file__, *sys.argv[1:], "--inside"])
    for case in ([args.case] if args.case else MAPS):
        if case == "skies" and args.renderer != "rdsp-rend2":
            continue
        run_case(args.package.resolve(), case, args.renderer, args.save)
    return 0


if __name__ == "__main__":
    sys.exit(main())
