#!/usr/bin/env python3
"""Capture whole and exploded maps in a headless window."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--map", required=True)
    parser.add_argument("--campaign", choices=("ja", "jo"), default="jo")
    parser.add_argument("--renderer", choices=("rdsp-rend2", "rdsp-vanilla"), default="rdsp-rend2")
    parser.add_argument("--buffer-storage", action="store_true", help="Use persistent mapped renderer buffers")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 960x720x24", sys.executable,
                                __file__, "--inside", *sys.argv[1:]])
    run = Path(tempfile.mkdtemp(prefix="automap-layout.", dir=root / "build/smoke"))
    home = run / "profile"
    profile = home / ("campaigns/jo/OpenJK" if args.campaign == "jo" else "OpenJK")
    profile.mkdir(parents=True)
    settings = dict(cl_renderer=args.renderer, r_mode=-1, r_customwidth=960, r_customheight=720,
                    r_fullscreen=0, in_nograb=1, s_initsound=0, developer=1, com_maxfps=60,
                    r_ignoreGLErrors=int(args.renderer == "rdsp-vanilla"), r_arb_buffer_storage=int(args.buffer_storage))
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("")
    env = dict(os.environ, OJK_PROFILE=str(home), OJK_JO_ASSETS=str(root / "GameData_JO"),
               LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    log = run / "console.log"
    print(f"Automap layout results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(["bash", str(args.package.resolve() / "launch-sp.sh"), str(root / "GameData"),
                                    "--campaign", args.campaign, "+devmap", args.map, "+wait", "100",
                                    "+echo", "LAYOUT_READY"], env=env, stdin=subprocess.PIPE,
                                   stdout=stream, stderr=subprocess.STDOUT, text=True, start_new_session=True)
        assert process.stdin
        stdin = process.stdin
        def text():
            return re.sub(r"\^[0-9]", "", log.read_text(errors="replace"))
        def wait(marker, start=0):
            deadline = time.monotonic() + 300
            while time.monotonic() < deadline:
                output = text()[start:]
                if marker in output:
                    return output
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited: {log}")
                time.sleep(0.05)
            raise TimeoutError(f"Missing {marker}: {log}")
        serial = 0
        def cmd(value):
            nonlocal serial
            serial += 1
            marker = f"LAYOUT_CMD_{serial}_DONE"
            start = len(text())
            line = f"{value}; wait 10; echo {marker}\n"
            assert len(line) < 256
            stdin.write(line)
            stdin.flush()
            return wait(marker, start)
        def capture(name):
            cmd(f"screenshot_png {name}; wait 20")
            return subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(profile / "screenshots" / f"{name}.png"),
                                            "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"])
        try:
            wait("LAYOUT_READY")
            window = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--pid", str(process.pid)], text=True).splitlines()[-1]
            subprocess.run(["xdotool", "windowfocus", "--sync", window], check=True)
            cmd("exitview; wait 200")
            cmd("helpusobi 1; god; notarget; d_npcfreeze 1; con_notifytime -1; datapad")
            subprocess.run(["xdotool", "key", "F4"], check=True)
            cmd("wait 20")
            assert "datapadMapMenu" in cmd("ui_report")
            # Put the cursor on the lower frame, outside the map and its status text.
            for _ in range(2):
                subprocess.run(["xdotool", "mousemove", "--window", window, "1", "1"], check=True)
                cmd("wait 5")
                subprocess.run(["xdotool", "mousemove", "--window", window, "958", "718"], check=True)
                cmd("wait 5")
            subprocess.run(["xdotool", "mousemove_relative", "--", "-40", "-25"], check=True)
            cmd("wait 5")
            whole = capture("whole")
            cmd("automap explode")
            status = cmd("automap_status")
            assert "exploded=1" in status, status
            (run / "status.txt").write_text(status)
            images = [capture("exploded")]
            cmd("automap centre")
            for _ in range(4):
                cmd("automap zoomin")
            images.append(capture("zoomed"))
            cmd("automap tilt")
            images.append(capture("top"))
            cmd("automap explode")
            images.append(capture("returned"))
            # The map must not alter the title, toolbar, tabs, or cursor area.
            rows = list(range(0, 95)) + list(range(630, 720))
            errors = [sum(abs(whole[(y*960+x)*3+c]-image[(y*960+x)*3+c])
                          for y in rows for x in range(960) for c in range(3)) / (len(rows)*960*3)
                      for image in images]
            (run / "ui-errors.json").write_text(json.dumps(errors))
            print(f"UI differences: {errors}", flush=True)
            assert max(errors) < 0.1, f"Map changed UI outside its viewport: {errors}"
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=15)
    assert not re.search(r"ERROR:|Error:|GL_INVALID_|command buffer full", text()), log
    print("PASS: map layout captures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
