#!/usr/bin/env python3
"""Check the real mission-end menu and mouse-driven progression under Xvfb."""

import argparse
import json
import os
from pathlib import Path
import queue
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-rend2", "rdsp-vanilla"))
    parser.add_argument("--msaa", type=int, choices=(0, 4))
    parser.add_argument("--raster", action="store_true", help="Enable the new raster effects in Rend2")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--reference", type=Path, help="Compare portrait proportions with a previous results.json")
    parser.add_argument("--display-ready", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not (640 <= args.width <= 3840 and 480 <= args.height <= 2160):
        parser.error("Unsupported test dimensions")
    if args.msaa is not None and args.renderer is None:
        parser.error("Use --renderer with --msaa")
    for command in ("xvfb-run", "xdotool", "ffmpeg"):
        if not shutil.which(command):
            parser.error(f"Missing dependency: {command}")
    if not args.display_ready:
        return subprocess.call(["xvfb-run", "-a", "-s", f"-screen 0 {args.width}x{args.height}x24",
                                sys.executable, str(Path(__file__).resolve()), *sys.argv[1:], "--display-ready"])
    package = args.package.resolve()
    fixture = "rend2-debrief.cfg"
    if (package / "OpenJK" / fixture).read_bytes() != (root / "scripts" / fixture).read_bytes():
        parser.error("Build a package with the current debrief fixture")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="debrief.", dir=output))
    print(f"Debrief results: {suite}", flush=True)
    results = []
    reference = json.loads(args.reference.read_text()) if args.reference else []
    cases = [(args.renderer, args.msaa or 0)] if args.renderer else [
        ("rdsp-vanilla", 0), ("rdsp-rend2", 0), ("rdsp-rend2", 4)]
    for renderer, msaa in cases:
        case = suite / f"{renderer}-{msaa}"
        profile = case / "profile/OpenJK"
        profile.mkdir(parents=True)
        settings = dict(cl_renderer=renderer, r_fullscreen=0, r_mode=-1,
                        r_customwidth=args.width, r_customheight=args.height,
                        r_ext_multisample=msaa, r_ssao=int(renderer == "rdsp-rend2"),
                        r_ssaoMethod=1, r_gtaoHalfRes=1, r_debugContext=1,
                        r_ignoreGLErrors=int(renderer == "rdsp-vanilla"),
                        com_maxfps=30, s_initsound=1, com_timestamps=0,
                        com_ansiColor=0, con_notifytime=-1, developer=0)
        if args.raster and renderer == "rdsp-rend2":
            settings.update(r_smaa=1, r_sss=0.5, r_capsuleShadows=1, r_softParticles=1)
        (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
        (profile / "autoexec_sp.cfg").write_text("// Isolated mission-end test.\n")
        env = dict(os.environ, OJK_PROFILE=str(profile.parent), SDL_VIDEODRIVER="x11",
                   LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
        env.pop("EGL_PLATFORM", None)
        process = subprocess.Popen(["bash", str(package / "launch-sp.sh"),
            os.environ.get("OJK_ASSETS", str(root / "GameData")),
            "+set", "r_fullscreen", "0", "+set", "r_mode", "-1",
            "+devmap", "t1_sour", "+exec", fixture],
            env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", bufsize=1, start_new_session=True)
        stdin, stdout = process.stdin, process.stdout
        assert stdin is not None and stdout is not None
        events = queue.Queue()
        lines = []
        def read_output():
            assert stdout is not None
            for line in stdout:
                lines.append(line)
                events.put(re.sub(r"\^[0-9]", "", line.strip()))
            events.put(None)
        reader = threading.Thread(target=read_output, daemon=True)
        reader.start()
        report_number = 0
        def send(command):
            assert stdin is not None
            stdin.write(command + "\n")
            stdin.flush()
        def until(marker, timeout=180):
            collected = []
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                line = events.get(timeout=max(0.01, deadline - time.monotonic()))
                if line is None:
                    raise RuntimeError("Engine exited before the expected marker")
                if line == marker:
                    return collected
                collected.append(line)
            raise RuntimeError(f"Timeout waiting for {marker}")
        def state():
            nonlocal report_number
            report_number += 1
            marker = f"OJK_UI_REPORT_{report_number}"
            send(f"wait 4; ui_report; wait 4; echo {marker}")
            text = "\n".join(until(marker))
            matches = re.findall(r"UI focus: ([^;]+); item: ([^;]+); cursor: (-?\d+) (-?\d+)", text)
            if not matches:
                raise RuntimeError("No UI focus report")
            menu, item, x, y = matches[-1]
            return menu, item, int(x), int(y)
        def capture(name):
            send(f"screenshot_png {name}; wait 12; echo OJK_CAPTURE_{name}")
            until(f"OJK_CAPTURE_{name}")
        try:
            until("OJK_DEBRIEF_TRIGGERED")
            # The target appends the menu command after the fixture. Let it run.
            send("wait 20; echo OJK_DEBRIEF_OPEN")
            until("OJK_DEBRIEF_OPEN")
            if state()[0] != "ingameMissionSelect":
                raise RuntimeError("The real level-end target did not open the debrief")
            windows = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--pid", str(process.pid)], text=True).splitlines()
            subprocess.run(["xdotool", "windowfocus", "--sync", windows[-1]], check=True)
            send("set in_nograb 1")
            capture("debrief_luke")
            time.sleep(1)
            capture("debrief_luke_later")
            clicked = []
            for button, image in (("victory2", "debrief_kyle"), ("story2", "mission_select")):
                # Reset the virtual relative cursor, as in the Force-wheel input test.
                subprocess.run(["xdotool", "mousemove", "--window", windows[-1],
                                str(args.width - 2), str(args.height - 2)], check=True)
                state()
                for _ in range(2):
                    subprocess.run(["xdotool", "mousemove", "--window", windows[-1], "1", "1"], check=True)
                    state()
                for dx, dy in ((285, 225), (285, 226)):
                    subprocess.run(["xdotool", "mousemove_relative", "--", str(dx), str(dy)], check=True)
                    current = state()
                if current[1] != button:
                    raise RuntimeError(f"Could not focus {button}: {state()}")
                subprocess.run(["xdotool", "click", "1"], check=True)
                time.sleep(0.5)
                capture(image)
                clicked.append(button)
            if state()[0] != "ingameMissionSelect1":
                raise RuntimeError("Continue and Okay did not reach mission selection")
            send("quit")
            if process.wait(timeout=30) != 0:
                raise RuntimeError("Engine shutdown failed")
            reader.join(timeout=10)
            text = "".join(lines)
            if "trying to load fallback" in text or (renderer == "rdsp-rend2" and "----- rdsp-rend2 -----" not in text):
                raise RuntimeError("Renderer identity check failed")
            if renderer == "rdsp-rend2" and re.search(r"GL_INVALID_|GL_OUT_OF_MEMORY|OpenGL -> [^\n]*\[(?:Error|Undefined)\]", text):
                raise RuntimeError("Rend2 reported a GL error")
            counts = {}
            for image in ("debrief_luke", "debrief_luke_later", "debrief_kyle"):
                rgb = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i",
                    str(profile / "screenshots" / f"{image}.png"), "-vf", "scale=640:480",
                    "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"],
                    capture_output=True, check=True).stdout
                if len(rgb) != 640 * 480 * 3:
                    raise RuntimeError("Invalid screenshot")
                points = []
                for y in range(315, 434):
                    for x in range(415, 565):
                        r, g, b = rgb[(y * 640 + x) * 3:(y * 640 + x) * 3 + 3]
                        if r > 70 and r > b + 25 and g > b + 10:
                            points.append((x, y))
                white = sum(min(rgb[(y * 640 + x) * 3:(y * 640 + x) * 3 + 3]) > 140
                            for y in range(313, 435) for x in range(44, 348))
                counts[image] = dict(portrait_pixels=len(points), text_pixels=white)
                if len(points) < 200 or white < 150:
                    raise RuntimeError(f"Missing portrait or briefing text: {image}: {counts[image]}")
                mx, my = (sum(p[i] for p in points) / len(points) for i in (0, 1))
                aspect = (sum((x - mx) ** 2 for x, y in points) / sum((y - my) ** 2 for x, y in points)) ** 0.5
                # Undo the UI-coordinate resize to compare physical face proportions.
                aspect *= (args.width / 640) / (args.height / 480)
                counts[image]["portrait_aspect"] = aspect
                if reference:
                    prior = next(r for r in reference if r["renderer"] == renderer and r["msaa"] == msaa)
                    ratio = aspect / prior["captures"][image]["portrait_aspect"]
                    if not 0.85 < ratio < 1.15:
                        raise RuntimeError(f"Portrait proportions changed: {image}: ratio={ratio:.3f}")
            results.append(dict(renderer=renderer, msaa=msaa, clicked=clicked, captures=counts))
            print(f"PASS: {renderer}, MSAA {msaa}: portraits, text, Continue, Okay, mission selection", flush=True)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            reader.join(timeout=5)
            stdin.close()
            stdout.close()
            (case / "console.log").write_text("".join(lines))
            (suite / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
