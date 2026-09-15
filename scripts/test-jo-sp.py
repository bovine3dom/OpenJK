#!/usr/bin/env python3
"""Test the JO opening, controls, save/load, and map transition headlessly."""

import argparse
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-vanilla")
    parser.add_argument("--sss", action="store_true", help="Also check imported JO skin masks with Rend2")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--ai", action="store_true", help="Test native Kejim guard pressure reactions")
    mode.add_argument("--content", action="store_true", help="Test Kejim equipment, materials, and datapad")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.sss and args.renderer != "rdsp-rend2":
        parser.error("--sss requires --renderer rdsp-rend2")
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 640x480x24", sys.executable,
                                __file__, "--inside", "--package", str(args.package), "--renderer", args.renderer]
                               + (["--sss"] if args.sss else [])
                               + (["--ai"] if args.ai else ["--content"] if args.content else []))
    output = ROOT / "build/jo-tests"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=args.renderer + ".", dir=output))
    profile = run / "profile"
    log = run / "console.log"
    env = dict(os.environ, OJK_PROFILE=str(profile), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1",
               SDL_AUDIODRIVER="dummy", OJK_JO_ASSETS=os.environ.get("OJK_JO_ASSETS", str(ROOT / "GameData_JO")))
    command = ["bash", str(args.package.resolve() / "launch-sp.sh"),
               os.environ.get("OJK_ASSETS", str(ROOT / "GameData")), "--campaign", "jo", "--new-game",
               "+safe", "+set", "cl_renderer", args.renderer, "+set", "r_fullscreen", "0",
               "+set", "r_mode", "3", "+set", "s_initsound", "1", "+set", "developer", "1",
               "+set", "logfile", "2", "+set", "com_maxfps", "60", "+set", "g_subtitles", "2",
               "+set", "cg_thirdPerson", "0", "+wait", "150", "+echo", "JO_READY"]
    print(f"JO integration results: {run}", flush=True)
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
                if marker in text:
                    return text
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited ({process.returncode}): {log}")
                time.sleep(0.05)
            raise TimeoutError(f"Missing {marker}: {log}")

        def cmd(text):
            nonlocal serial
            serial += 1
            marker = f"JO_COMMAND_{serial}_DONE"
            start = len(log.read_text(errors="replace"))
            stdin.write(f"{text}; wait 5; echo {marker}\n")
            stdin.flush()
            return re.sub(r"\^[0-9]", "", wait_for(marker, start))

        def capture(name, region=None, contrast=True):
            cmd(f"screenshot_png {name}")
            image = profile / "campaigns/jo/OpenJK/screenshots" / f"{name}.png"
            deadline = time.monotonic() + 30
            while not (image.is_file() and image.read_bytes().endswith(b"IEND\xaeB`\x82")):
                if time.monotonic() > deadline:
                    raise TimeoutError(f"Incomplete screenshot: {image}")
                time.sleep(0.05)
            width, height = region[:2] if region else (64, 48) if contrast else (640, 480)
            image_filter = "crop=" + ":".join(map(str, region)) if region else f"scale={width}:{height}"
            pixels = subprocess.check_output(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
                                              "-vf", image_filter, "-frames:v", "1", "-pix_fmt", "gray",
                                              "-f", "rawvideo", "-"])
            assert len(pixels) == width * height, image
            if contrast:
                assert max(pixels) - min(pixels) > 16, image
            return pixels

        def status(mapname):
            text = cmd("campaign_status")
            assert f"campaign=jo map={mapname} camera=0 health=100" in text, text
            assert "weapon=18 weapons=393217 force=0" in text, text
            sound = cmd("soundinfo")
            assert "Dynamic music ON" in sound and f'Dynamic music set name: "{mapname}"' in sound, sound
            assert "actual: 'explore'" in sound, sound
            return text

        def npc():
            text = cmd("nav memory st_guard2")
            result = {}
            for line in text.splitlines():
                if "aimemory event=sample " in line or "aimemory event=lifecycle " in line:
                    result.update(dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split()))
            assert result, text
            return result

        def check_ai():
            cmd("helpusobi 1; god; set d_npcai 3; set d_squadTactics 0; wait 200")
            actors = cmd("nav actors")
            assert "class=-1" not in actors and "name=jan type=jan class=20" in actors, actors
            # Approach the original guard after the opening. Let scripts and perception run.
            cmd("setviewpos 400 -2193 0 322; wait 40")
            before = {}
            for _ in range(80):
                before = npc()
                if before["enemy"] == "0" and before["scripted"] == "0" and before["group"] != "-1":
                    break
                cmd("wait 10")
            else:
                raise AssertionError(f"Native guard did not finish its orders and acquire Kyle: {before}")
            assert before["class"] == "48" and before["health"] == "30" and before["los"] == "1", before
            cmd("nav memory st_guard2 protect; save jo_ai")
            # A real Bryar shot passes beside the guard. No hit or enemy-assignment command is used.
            cmd("set d_squadTactics 1; +attack; wait 2; -attack")
            samples = []
            for _ in range(20):
                cmd("wait 5")
                samples.append(npc())
            text = log.read_text(errors="replace")
            assert re.search(r"squad event=incoming_fire ent=" + before["ent"] + r" .*distance=", text), log
            assert all(s["health"] == before["health"] for s in samples), samples
            moving = [s for s in samples if s["role"] == "1" and float(s["speed"]) > float(s["walkSpeed"])]
            assert moving, "Native guard did not run from a near miss"
            origin = tuple(map(float, before["pos"].split(",")))
            assert max(math.dist(origin, tuple(map(float, s["pos"].split(",")))) for s in moving) >= 32, moving
            assert all(s["script_flags"] == before["script_flags"] for s in samples), "Script orders changed"
            capture("native_pressure")
            cmd("set d_squadTactics 0; load jo_ai; wait 20; set d_squadTactics 0")
            restored = npc()
            assert restored["class"] == "48" and restored["health"] == "30", restored
            cmd("set d_squadTactics 1; nav memory st_guard2 hit; wait 5")
            damaged = []
            for _ in range(10):
                damaged.append(npc())
                cmd("wait 5")
            assert all(s["health"] == "25" for s in damaged), "Native damage handler did not receive the hit"
            running = any(s["role"] == "1" and float(s["speed"]) > float(s["walkSpeed"]) for s in damaged)
            # A sample can land in the deceleration at each short waypoint. Also measure travel between samples.
            for a, b in zip(damaged, damaged[1:]):
                elapsed = int(b["time"]) - int(a["time"])
                distance = math.dist(tuple(map(float, a["pos"].split(","))), tuple(map(float, b["pos"].split(","))))
                running |= (a["role"] == b["role"] == "1" and b["walking"] == "0" and elapsed > 0
                            and distance * 1000 / elapsed > float(b["walkSpeed"]))
            assert running, ("Native guard did not retreat after damage", damaged)
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            assert not re.search(r"ERROR:|Error:|Unknown command|aimemory event=rejected", log.read_text(errors="replace")), log
            print("PASS: JO native NPC classes, sight acquisition, near-miss retreat, damage, and save/load")

        def key(name):
            window = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--pid", str(process.pid)], text=True).splitlines()[-1]
            subprocess.run(["xdotool", "windowfocus", window, "key", name], check=True, timeout=10)
            cmd("wait 10")

        def check_content():
            cmd("helpusobi 1; god; set d_npcfreeze 1; set com_maxfps 20")
            cmd("give weaponnum 3; give weaponnum 10; give ammo; wait 30; weapon 3; wait 40")
            for command, expected in (("weapnext", (10, 17, 18, 3)), ("weapprev", (18, 17, 10, 3))):
                for weapon in expected:
                    text = cmd(f"{command}; wait 30; campaign_status")
                    assert f"weapon={weapon} " in text, text
            cmd("datapad; wait 10; uimenu datapadMissionMenu; wait 10; clear")
            datapad = capture("datapad")
            text_area = [datapad[y * 64 + x] for y in range(8, 13) for x in range(5, 60)]
            assert sum(p > 60 for p in text_area) > 15, "Datapad objectives obscured"
            key("Escape")
            cmd("use act_perimeter_guns; setviewpos -492 -368 64 180; wait 20; use perimeter_gun1; wait 30")
            assert "mounted=1" in cmd("campaign_status")
            full = capture("turret_full", (224, 16, 208, 432))
            cmd("give health 50; wait 10")
            half = capture("turret_half", (224, 16, 208, 432))
            assert sum(abs(a - b) > 16 for a, b in zip(full, half)) >= 50, "Turret health bar did not change"
            cmd("exitview; wait 20; give health 100; setviewpos 2718 -758 -544 135; wait 50")
            before = cmd("campaign_status; bind KP_LEFTARROW; bind i")
            assert "goggles=1" in before and "use_lightamp_goggles" in before and "invuse" in before, before
            capture("goggles_off")
            # The stock inventory opens on the first press and advances on the next.
            cmd("invnext; wait 2; invnext; wait 10")
            key("i")
            active = cmd("wait 20; campaign_status")
            assert "zoom=3" in active, active
            assert int(re.search(r"battery=(\d+)", active)[1]) < int(re.search(r"battery=(\d+)", before)[1]), active
            capture("goggles_on")
            key("i")
            assert "zoom=0" in cmd("campaign_status")
            cmd("setviewpos 1120 -400 -560 90; wait 20; clear")
            panels = capture("panels")
            panel_area = [panels[y * 64 + x] for y in range(14, 27) for x in range(30, 59)]
            assert sum(p > 190 for p in panel_area) < len(panel_area) // 8, "Missing-texture grid on panels"
            cmd("noclip; setviewpos 992 -400 -736 0; set cg_draw2D 0; wait 20; clear")
            powered = capture("pipes_powered")
            cmd("use t312; wait 30; clear")
            disabled = capture("pipes_disabled")
            assert sum(a - b > 60 for a, b in zip(powered, disabled)) > 150, "Generator pipe material did not switch off"
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            assert not re.search(r"ERROR:|Error:|Unknown command|Duplicate shader entry|Couldn't find image for shader gfx/(?:menus|hud)", text), log
            print(f"PASS: JO weapon cycle, datapad, turret health, goggles, panels, and generator pipe material ({args.renderer})")

        try:
            wait_for("CM_LoadMap( maps/kejim_post.bsp, 1 )")
            subprocess.run(["ffmpeg", "-v", "error", "-f", "x11grab", "-video_size", "640x480",
                            "-draw_mouse", "0", "-i", os.environ["DISPLAY"], "-frames:v", "1",
                            str(run / "loading.png")], check=True, timeout=30)
            loading = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(run / "loading.png"),
                                               "-vf", "scale=64:48", "-frames:v", "1", "-pix_fmt", "rgb24",
                                               "-f", "rawvideo", "-"])
            assert sum(b - r > 25 for r, b in zip(loading[0::3], loading[2::3])) > 64 * 48 // 10, "Missing blue JO loading artwork"
            wait_for("JO_READY")
            capture("cinematic")
            cmd("exitview; wait 200")
            text = status("kejim_post")
            assert "objective=KEJIM_POST_OBJ1 status=0" in text and "objective=KEJIM_POST_OBJ2 status=0" in text, text
            capture("gameplay")
            if args.ai:
                check_ai()
                return 0
            if args.content:
                check_content()
                return 0
            cmd("toggleconsole; wait 20")
            if args.renderer == "rdsp-rend2":
                settings = cmd("r_ssao; r_ssaoMethod")
                assert 'r_ssao = "1"' in settings and 'r_ssaoMethod = "1"' in settings, settings
            console = capture("console")
            # Exclude the game view below the console. Reject a missing-texture fill.
            assert sum(value > 200 for value in console[:64 * 20]) < 64 * 20 // 4, "Console background is too bright"
            cmd("toggleconsole; wait 20")
            cmd("set cg_dynamicCrosshair 0; set cg_crosshairSize 32; set cg_rmluiReticleScale 1; wait 10")
            capture("reticle")
            cmd("+weaponwheel; wait 5")
            text = cmd("weaponwheel_status")
            assert "count=2" in text and "open=1" in text, text
            capture("weaponwheel")
            cmd("-weaponwheel; wait 5")
            text = cmd("+forcewheel; forcewheel_status; -forcewheel")
            assert "open=0" in text and "mask=0" in text, text
            cmd("weapon 17; wait 50")
            assert "weapon=17" in cmd("campaign_status")
            cmd("weapon 18; wait 50")
            before = status("kejim_post")
            cmd("+attack; wait 20; -attack; wait 10")
            after = status("kejim_post")
            ammo = int(re.search(r"ammo=(\d+)", after)[1])
            assert ammo < int(re.search(r"ammo=(\d+)", before)[1]), after
            capture("firing")
            cmd("save jo_mvp")
            cmd("load jo_mvp; wait 100")
            assert f"ammo={ammo}" in status("kejim_post")
            capture("loaded")
            cmd("maptransition kejim_base; wait 100")
            text = status("kejim_base")
            assert "objective=KEJIM_BASE_OBJ1 status=0" in text, text
            capture("base")
            if args.sss:
                cmd("helpusobi 1; cg_draw2D 0; con_notifytime -1; d_npcfreeze 1; "
                    "cg_thirdPerson 1; cg_thirdPersonAngle 180; cg_thirdPersonRange 90; "
                    "r_smaa 0; r_sss 1; r_sssRadius 0.5; r_sssDebug 1; wait 60")
                for name, model in (("kyle", "kyle"), ("jan", "jan"),
                                    ("acrobat", "reborn|model_acrobat"), ("fencer", "reborn|model_fencer"),
                                    ("prisoner", "prisoner"), ("gran", "gran"), ("ugnaught", "ugnaught"),
                                    ("cinematic_kyle", "jo_cinematic_kyle"), ("cinematic_jan", "jo_cinematic_jan"),
                                    ("armour", "stormtrooper")):
                    cmd(f'playerModel "{model}"; wait 60')
                    pixels = capture("sss_" + name, contrast=False)
                    count = sum(value > 128 for value in pixels)
                    assert (count == 0 if name == "armour" else count > 20), (name, count)
                    print(f"SSS {name}: {count} mask pixels", flush=True)
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            assert "Loaded saved game format 3" in text
            assert not re.search(r"ERROR:|Error:|Unknown command|trying to load fallback renderer", text), log
            assert not re.search(r"Unable to find entry for|couldn't open music file|"
                                 r"could not find 'menu/new/title'|Can't find levelshots/kejim_\w+\.jpg", text), log
            print(f"PASS: JO opening, wheels, weapon switching, save/load, and Kejim transition ({args.renderer})")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
