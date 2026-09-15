#!/usr/bin/env python3
"""Test the JO opening, controls, save/load, and map transition headlessly."""

import argparse
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
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 640x480x24", sys.executable,
                                __file__, "--inside", "--package", str(args.package), "--renderer", args.renderer])
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
    if args.renderer == "rdsp-rend2":
        command += ["+set", "r_ssao", "1", "+set", "r_ssaoMethod", "1"]
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

        def capture(name):
            cmd(f"screenshot_png {name}")
            image = profile / "campaigns/jo/OpenJK/screenshots" / f"{name}.png"
            deadline = time.monotonic() + 30
            while not (image.is_file() and image.read_bytes().endswith(b"IEND\xaeB`\x82")):
                if time.monotonic() > deadline:
                    raise TimeoutError(f"Incomplete screenshot: {image}")
                time.sleep(0.05)
            pixels = subprocess.check_output(["ffmpeg", "-v", "error", "-xerror", "-i", str(image),
                                              "-vf", "scale=64:48", "-frames:v", "1", "-pix_fmt", "gray",
                                              "-f", "rawvideo", "-"])
            assert len(pixels) == 64 * 48 and max(pixels) - min(pixels) > 16, image
            return pixels

        def status(mapname):
            text = cmd("campaign_status")
            assert f"campaign=jo map={mapname} camera=0 health=100" in text, text
            assert "weapon=18 weapons=393217 force=0" in text, text
            sound = cmd("soundinfo")
            assert "Dynamic music ON" in sound and f'Dynamic music set name: "{mapname}"' in sound, sound
            assert "actual: 'explore'" in sound, sound
            return text

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
