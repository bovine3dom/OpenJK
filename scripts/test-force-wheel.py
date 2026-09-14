#!/usr/bin/env python3
"""Test Force wheel input and menu bindings in a private headless session."""

import argparse
import ctypes
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
    parser.add_argument("package", type=Path)
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-vanilla")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 1280x720x24",
                                sys.executable, __file__, str(args.package), "--renderer", args.renderer, "--inside"])

    output = ROOT / "build/force-wheel-tests"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=f"{args.renderer}.", dir=output))
    profile = run / "profile"
    log = run / "console.log"
    print(f"Force wheel test: {run}", flush=True)
    env = dict(os.environ, OJK_PROFILE=str(profile), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    command = ["bash", str(args.package.resolve() / "launch-sp.sh"), os.environ.get("OJK_ASSETS", str(ROOT / "GameData")),
               "+safe", "+set", "cl_renderer", args.renderer, "+set", "r_fullscreen", "0", "+set", "r_mode", "-1",
               "+set", "r_customwidth", "1280", "+set", "r_customheight", "720", "+set", "s_initsound", "0",
               "+set", "developer", "0", "+set", "logfile", "2", "+set", "com_maxfps", "60",
               "+devmap", "t1_sour", "+wait", "60", "+exitview", "+setForceAll", "3", "+wait", "30",
               "+echo", "WHEEL_TEST_READY"]
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT, env=env, text=True)
        stdin = process.stdin
        assert stdin is not None
        marker_id = 0

        def wait_for(predicate, timeout=180):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if predicate(text):
                    return text
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited with {process.returncode}: {log}")
                time.sleep(0.05)
            raise TimeoutError(f"Test timed out: {log}")

        def cmd(text):
            nonlocal marker_id
            marker_id += 1
            marker = f"WHEEL_TEST_COMMAND_{marker_id}_DONE"
            start = len(log.read_text(errors="replace"))
            stdin.write(f"{text}; wait 4; echo {marker}\n")
            stdin.flush()
            return re.sub(r"\^[0-9]", "", wait_for(lambda data: marker in data[start:])[start:])

        def status():
            text = cmd("wait 4; forcewheel_status")
            line = re.findall(r"forcewheel open=[^\r\n]+", text)[-1]
            return {key: float(value) for key, value in (word.split("=") for word in line.split()[1:])}

        def xdo(*arguments):
            return subprocess.check_output(["xdotool", *map(str, arguments)], text=True, timeout=10).strip()

        def capture(name):
            cmd(f"screenshot_png {name}")
            image = profile / "OpenJK/screenshots" / f"{name}.png"
            # Rend2 writes screenshots asynchronously. Wait for the final PNG chunk.
            wait_for(lambda _: image.is_file() and image.read_bytes().endswith(b"IEND\xaeB`\x82"))
            subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image), "-frames:v", "1", "-f", "null", "-"], check=True)

        def focus_game():
            window = xdo("search", "--onlyvisible", "--pid", process.pid).splitlines()[-1]
            xdo("windowfocus", window)
            return window

        def move_ui(x, y):
            # UI mouse coordinates are virtual relative coordinates, not framebuffer pixels.
            window = focus_game()
            xdo("mousemove", "--window", window, 1278, 718)
            cmd("wait 2")
            for _ in range(2):
                xdo("mousemove", "--window", window, 1, 1)
                cmd("wait 2")
            # Split the motion so a captured pointer cannot reach the display edge.
            xdo("mousemove_relative", "--", x // 2, y // 2)
            cmd("wait 2")
            xdo("mousemove_relative", "--", x - x // 2, y - y // 2)
            cmd("wait 2")

        try:
            wait_for(lambda text: "WHEEL_TEST_READY" in text)
            assert "RmlUi: IBM Plex Mono loaded" in log.read_text(errors="replace")
            focus_game()
            bindings = cmd("bind g; bind MOUSE3")
            assert re.search(r'\bG\s*=\s*"\+forcewheel"', bindings, re.I), bindings
            assert re.search(r'\bMOUSE3\s*=\s*"saberAttackCycle"', bindings, re.I), bindings
            initial = status()
            assert initial["mask"] == 4095 and initial["open"] == 0, initial

            cmd("set timescale 0.5")
            xdo("keydown", "g")
            opened = status()
            assert opened["open"] == 1 and opened["hovered"] == -1, opened
            xdo("key", "f")  # Force use must not activate while the wheel owns input.
            xdo("mousemove_relative", "--", 80, 0)
            selected = status()
            assert selected["hovered"] == 3, selected
            assert selected["yaw"] == opened["yaw"] and selected["pitch"] == opened["pitch"], selected
            capture("wheel_open")
            time.sleep(0.7)
            later = status()
            ratio = (later["game"] - selected["game"]) / (later["real"] - selected["real"])
            assert 0.04 < ratio < 0.25 and later["timescale"] == 0.5, (ratio, later)
            cmd("set timescale 0.75")  # Another time owner can change the base while open.
            xdo("keyup", "g")
            committed = status()
            assert committed["open"] == 0 and committed["selected"] == 3, committed
            assert committed["active"] == initial["active"] and committed["force"] == initial["force"], committed
            assert committed["timescale"] == 0.75, committed
            cmd("set timescale 0.5")

            xdo("keydown", "g")
            assert status()["open"] == 1
            xdo("keyup", "g")
            assert status()["selected"] == 3  # Releasing in the dead zone does not select.
            xdo("keydown", "g")
            assert status()["open"] == 1
            xdo("mousemove_relative", "--", 0, -80)
            xdo("key", "Escape")
            cancelled = status()
            assert cancelled["open"] == 0 and cancelled["selected"] == 3 and cancelled["catcher"] == 0, cancelled
            time.sleep(0.5)
            assert status()["open"] == 0
            xdo("keyup", "g")

            xdo("keydown", "w")
            xdo("mousedown", 1)
            cmd("wait 5")
            xdo("keydown", "g")
            moving = status()
            assert moving["open"] == 1 and moving["forward"] > 0 and moving["buttons"] == 0, moving
            xdo("keydown", "d")
            moving = status()
            assert moving["forward"] > 0 and moving["right"] > 0 and moving["buttons"] == 0, moving
            xdo("keyup", "w")
            moving = status()
            assert moving["forward"] == 0 and moving["right"] > 0, moving
            xdo("keyup", "g")
            moving = status()
            assert moving["open"] == 0 and moving["right"] > 0 and moving["buttons"] == 0, moving
            xdo("keyup", "d")
            xdo("mouseup", 1)
            stopped = status()
            assert all(stopped[field] == 0 for field in ("forward", "right", "up", "buttons")), stopped

            xdo("keydown", "g")
            assert status()["open"] == 1
            xlib = ctypes.CDLL("libX11.so.6")
            xlib.XOpenDisplay.restype = ctypes.c_void_p
            xlib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
            xlib.XDefaultRootWindow.restype = ctypes.c_ulong
            xlib.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
            xlib.XFlush.argtypes = xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
            display = xlib.XOpenDisplay(None)
            assert display
            xlib.XSetInputFocus(display, xlib.XDefaultRootWindow(display), 0, 0)
            xlib.XFlush(display)
            xlib.XCloseDisplay(display)
            assert status()["open"] == 0
            focus_game()
            xdo("keyup", "g")

            cmd("+forcewheel")
            assert status()["open"] == 1
            cmd("cam_enable")
            assert status()["open"] == 0
            cmd("-forcewheel; cam_disable; wait 10")

            cmd("set timescale 0.5; +forcewheel")
            assert status()["open"] == 1
            cmd("set r_picmip 2; set r_textureMode GL_NEAREST; vid_restart; wait 30")
            focus_game()
            cmd("-forcewheel")
            restarted = status()
            assert restarted["open"] == 0 and restarted["timescale"] == 0.5, restarted
            cmd("set timescale 1; +forcewheel")
            assert status()["open"] == 1
            capture("wheel_font_after_restart")
            cmd("uimenu ingameControlsMenu; -forcewheel")
            assert status()["open"] == 0
            cmd("set in_nograb 1")  # Use ordinary X11 motion for menu automation after a GL restart.
            move_ui(170, 267)
            xdo("click", 1)
            cmd("wait 10")
            capture("wheel_controls")
            move_ui(470, 405)
            xdo("click", 1)
            xdo("click", 2)  # X11 button 2 is the game's MOUSE3.
            for _ in range(3):
                xdo("key", "Escape")
                if status()["catcher"] == 0:
                    break
            cmd("set in_nograb 0")
            binding = cmd("bind MOUSE3; writeconfig wheel-bindings.cfg")
            assert re.search(r'\bMOUSE3\s*=\s*"\+forcewheel"', binding, re.I), binding
            saved = (profile / "OpenJK/wheel-bindings.cfg").read_text()
            assert re.search(r'bind "MOUSE3" "\+forcewheel"', saved, re.I), saved
            assert re.search(r'bind "L" "saberAttackCycle"', saved, re.I), saved
            xdo("mousedown", 2)
            assert status()["open"] == 1
            xdo("mouseup", 2)
            assert status()["open"] == 0
            protected = cmd("unbind MOUSE3; bind g +use; forcewheel_defaults; bind g")
            assert re.search(r'\bG\s*=\s*"\+use"', protected, re.I), protected
            cmd("-forcewheel; bind g +forcewheel; +forcewheel")
            assert status()["open"] == 1
            cmd("setForceAll 0; wait 10")
            assert status()["open"] == 0
            cmd("-forcewheel; setForceAll 3; wait 10; +forcewheel")
            assert status()["open"] == 1
            cmd("kill; wait 10")
            assert status()["open"] == 0
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            assert text.count("RmlUi: IBM Plex Mono loaded") >= 2
            assert not re.search(r"Leaking |geometry exceeds|DrawUiGeometry:|cannot upload font texture|reticle initialization failed|Unknown command|trying to load fallback renderer", text), log
            print(f"PASS: {args.renderer} wheel selection, time, input, focus, restart, death, and menu bindings. {run}")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
