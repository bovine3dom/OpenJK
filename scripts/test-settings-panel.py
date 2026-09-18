#!/usr/bin/env python3
"""Test the RmlUi settings panel with real mouse input."""

import argparse
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
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 960x720x24", sys.executable,
                                __file__, "--inside", "--package", str(args.package)])

    package = args.package.resolve()
    suite = Path(tempfile.mkdtemp(prefix="settings-panel.", dir=root / "build/smoke"))
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960, r_customheight=720,
                    r_fullscreen=0, in_nograb=1, s_initsound=0, developer=1, com_maxfps=30,
                    r_atmosphere=1)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {key} "{value}"\n' for key, value in settings.items()))
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), LIBGL_ALWAYS_SOFTWARE="1",
               LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    log = suite / "console.log"
    print(f"Settings panel results: {suite}", flush=True)

    with log.open("w") as stream:
        process = subprocess.Popen(["bash", str(package / "launch-sp.sh"), str(root / "GameData"),
            "+devmap", "t1_sour", "+wait", "100", "+echo", "SETTINGS_READY"], env=env,
            stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT, text=True, start_new_session=True)
        assert process.stdin is not None
        serial = 0

        def text():
            return re.sub(r"\^[0-9]", "", log.read_text(errors="replace"))

        def wait(marker, start=0):
            deadline = time.monotonic() + 300
            while time.monotonic() < deadline:
                result = text()[start:]
                if marker in result:
                    return result
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited: {log}")
                time.sleep(.05)
            raise TimeoutError(f"Missing {marker}: {log}")

        def cmd(command):
            nonlocal serial
            serial += 1
            marker = f"SETTINGS_{serial}_DONE"
            start = len(text())
            process.stdin.write(f"{command}; wait 3; echo {marker}\n")
            process.stdin.flush()
            return wait(marker, start)

        def xdo(*arguments):
            return subprocess.check_output(["xdotool", *map(str, arguments)], text=True).strip()

        def rect(name):
            result = cmd("settings_status " + name)
            match = re.search(r"rect=([\d.,-]+)", result)
            assert match, result
            return tuple(map(float, match[1].split(",")))

        def move(x, y):
            for _ in range(2):
                xdo("mousemove", "--window", window, 958, 718)
                cmd("wait 2")
                xdo("mousemove", "--window", window, 1, 1)
                cmd("wait 2")
            xdo("mousemove_relative", "--", round(x) - 1, round(y) - 1)
            cmd("wait 2")

        def show(name):
            form = rect("form")
            move(form[0] + form[2] - 20, form[1] + form[3] / 2)
            for _ in range(80):
                box = rect(name)
                if form[1] + 5 < box[1] and box[1] + box[3] < form[1] + form[3] - 5:
                    return box
                xdo("click", 5 if box[1] + box[3] >= form[1] + form[3] - 5 else 4)
                cmd("wait 2")
            raise RuntimeError(f"Could not scroll to {name}")

        def click(name):
            x, y, width, height = show(name)
            move(x + width / 2, y + height / 2)
            xdo("click", 1)
            cmd("wait 5")

        try:
            wait("SETTINGS_READY")
            window = xdo("search", "--onlyvisible", "--pid", process.pid).splitlines()[-1]
            xdo("windowfocus", "--sync", window)
            cmd("exitview; wait 80; con_notifytime -1; settings r_")
            state = cmd("settings_status")
            assert "active=1" in state and "group=r_" in state, state

            x, y, width, height = rect("group")
            move(x + width / 2, y + height / 2)
            xdo("click", 1)
            cmd("screenshot_png settings_dropdown; wait 20")
            x, y, width, height = rect("option:s_")
            move(x + width / 2, y + height / 2)
            xdo("click", 1)
            cmd("wait 5")
            assert "group=s_" in cmd("settings_status")

            cmd("settings; settings r_")
            assert "group=r_" in cmd("settings_status")
            box = show("r_atmosphere")
            cmd("screenshot_png settings_panel; wait 20")
            move(box[0] + box[2] / 2, box[1] + box[3] / 2)
            cursor = rect("cursor")
            assert abs(cursor[0] - (box[0] + box[2] / 2)) < 3
            assert abs(cursor[1] - (box[1] + box[3] / 2)) < 3
            xdo("click", 1)
            cmd("wait 5")
            assert re.search(r'r_atmosphere.*"0"', cmd("r_atmosphere"))

            click("reset:r_atmosphere")
            assert re.search(r'r_atmosphere.*"1"', cmd("r_atmosphere"))
            x, y, width, height = rect("close")
            move(x + width / 2, y + height / 2)
            xdo("click", 1)
            cmd("wait 5")
            assert "active=0" in cmd("settings_status")
            assert not re.search(r"RmlUi:.*(?:ERROR|Syntax error|Invalid property|Failed)|Unknown command", text())
            process.stdin.write("quit\n")
            process.stdin.flush()
            process.wait(timeout=20)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=10)
    print("PASS: dropdown, cursor alignment, checkbox, reset, and close")
    return 0


if __name__ == "__main__":
    sys.exit(main())
