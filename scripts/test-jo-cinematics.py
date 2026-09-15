#!/usr/bin/env python3
"""Check the Kejim CCTV sequence and the Artus opening without cinematic skipping."""

import argparse
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run_case(package, case, renderer, saved):
    output = ROOT / "build/jo-cinematics"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=f"{case}.{renderer}.", dir=output))
    profile = run / "profile"
    if saved:
        saves = profile / "campaigns/jo/OpenJK/saves"
        saves.mkdir(parents=True)
        shutil.copyfile(saved, saves / "cinematic_test.sav")
    log = run / "console.log"
    mapname = "kejim_base" if case == "cctv" else "artus_mine"
    env = dict(os.environ, OJK_PROFILE=str(profile), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1",
               SDL_AUDIODRIVER="dummy", OJK_JO_ASSETS=os.environ.get("OJK_JO_ASSETS", str(ROOT / "GameData_JO")))
    command = ["bash", str(package / "launch-sp.sh"), os.environ.get("OJK_ASSETS", str(ROOT / "GameData")),
               "--campaign", "jo", "+safe", "+set", "cl_renderer", renderer,
               "+set", "r_fullscreen", "0", "+set", "r_mode", "3", "+set", "s_initsound", "1",
               "+set", "g_subtitles", "2", "+set", "developer", "1", "+set", "logfile", "2",
               "+set", "com_maxfps", "20",
               *(["+load", "cinematic_test"] if saved else ["+map", mapname]),
               "+wait", "1", "+echo", "CINEMATIC_READY"]
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
            history = []
            captured = False
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                if case == "cctv":
                    current = samples(cmd("wait 10; cinematic_status cinematic2_kyle; cinematic_status cinematic_galak"))
                else:
                    current = samples(cmd("wait 1; cinematic_status cinematic4_kyle"))
                history.extend(current)
                if not captured and any(s.get("legs", "").startswith("BOTH_CIN_") if case == "cctv"
                                        else s.get("nav") == "1" for s in current):
                    capture("galak" if case == "cctv" else "walking")
                    captured = True
                if current and all(s.get("absent") == "1" for s in current):
                    break
            else:
                raise TimeoutError(f"Cinematic did not complete: {log}")
            if case == "cctv":
                galak = [s for s in history if s["name"] == "cinematic_galak" and "absent" not in s]
                assert galak and any(s["voice"] == "1" for s in galak), "Galak never spoke"
                assert captured, "Missing JO gesture animation"
                following = samples(cmd("wait 10; cinematic_status cinematic3_mon_mothma"))
                assert following and "absent" not in following[0] and following[0]["camera"] == "1", following
            else:
                moving = [s for s in history if s.get("nav") == "1"
                          and math.hypot(*map(float, s["velocity"].split(",")[:2])) > 10]
                assert moving and all(s["noclip"] == "0" and s["legs"] == "BOTH_WALK1" for s in moving), moving
                assert any(s["ground"] != "1023" for s in moving), "Kyle never touched the ground"
                assert history[-1]["camera"] == "0", history[-1]
            capture("completed")
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
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
    parser.add_argument("--case", choices=("cctv", "artus"))
    parser.add_argument("--save", type=Path, help="Load a save from before the selected cinematic")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.save and not args.case:
        parser.error("--save requires --case")
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 640x480x24", sys.executable,
                                __file__, *sys.argv[1:], "--inside"])
    for case in ([args.case] if args.case else ("cctv", "artus")):
        run_case(args.package.resolve(), case, args.renderer, args.save)
    return 0


if __name__ == "__main__":
    sys.exit(main())
