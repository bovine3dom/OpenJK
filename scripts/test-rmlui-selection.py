#!/usr/bin/env python3
"""Check JA selection rules and the aspect-safe RmlUi remake with real input."""
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
    parser.add_argument("--reference", action="store_true", help="Capture the stock screens from a reference package")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 1600x1200x24", sys.executable, __file__, *sys.argv[1:], "--inside"])
    output = ROOT / "build/rmlui-selection"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=args.renderer + ".", dir=output))
    profile, log = run / "profile", run / "console.log"
    env = dict(os.environ, OJK_PROFILE=str(profile), LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    command = ["bash", str(args.package.resolve() / "launch-sp.sh"), str(ROOT / "GameData"), "--campaign", "ja",
               "+safe", "+set", "cl_renderer", args.renderer, "+set", "r_fullscreen", "0", "+set", "r_mode", "3",
               "+set", "developer", "1", "+set", "logfile", "2", "+set", "com_maxfps", "30",
               "+devmap", "t1_sour", "+wait", "150", "+echo", "SELECTION_READY"]
    print(f"RmlUi selection results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(command, env=env, stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT, text=True)
        assert process.stdin
        stdin = process.stdin
        serial = 0

        def wait(marker, start=0):
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                text = re.sub(r"\^[0-9]", "", log.read_text(errors="replace")[start:])
                if "ERROR:" in text or process.poll() is not None: raise RuntimeError(log)
                if marker in text: return text
                time.sleep(.02)
            raise TimeoutError(f"Missing {marker}: {log}")

        def cmd(text):
            nonlocal serial
            serial += 1
            marker = f"SELECTION_CMD_{serial}_DONE"
            start = len(log.read_text(errors="replace"))
            line = f"{text}; wait 5; echo {marker}\n"
            assert len(line.encode()) < 256
            stdin.write(line)
            stdin.flush()
            return wait(marker, start)

        def state():
            text = cmd("rml_selection_status")
            match = re.search(r"rml_selection ([^\n]+)", text)
            assert match, text
            values = dict(word.split("=", 1) for word in match[1].split())
            powers = {name: (int(level), int(original), int(editable)) for name, level, original, editable in
                      re.findall(r"rml_power name=(\w+) level=(\d+) original=(\d+) editable=(\d+)", text)}
            return values, powers

        def key(*keys):
            window = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--name", "."], text=True).splitlines()[-1]
            subprocess.run(["xdotool", "windowfocus", window, "key", *keys], check=True, timeout=10)
            cmd("wait 10")

        def click(x, y):
            for _ in range(35):
                values, _ = state()
                left, top, _, _ = map(float, values["viewport"].split(","))
                cx, cy = map(float, values["cursor"].split(","))
                dx, dy = round(left + x * float(values["scale"]) - cx), round(top + y * float(values["scale"]) - cy)
                if abs(dx) <= 1 and abs(dy) <= 1: break
                subprocess.run(["xdotool", "mousemove_relative", "--", str(max(-100, min(100, dx))), str(max(-100, min(100, dy)))], check=True)
                cmd("wait 3")
            else:
                raise AssertionError(f"Pointer could not reach {x},{y}")
            subprocess.run(["xdotool", "click", "1"], check=True)
            cmd("wait 10")

        def capture(name):
            cmd(f"screenshot_png {name}; wait 10")
            path = profile / "OpenJK/screenshots" / f"{name}.png"
            subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path), "-frames:v", "1", "-f", "null", "-"], check=True)
            return path

        try:
            wait("SELECTION_READY")
            cmd("helpusobi 1; exitview; wait 200; god; notarget; set d_npcfreeze 1; setForceAll 1")
            cmd('set weapon_menu 0; set tier_mapname "maptransition t1_sour"; uimenu ingameForceSelect; wait 30')
            if args.reference:
                capture("force_reference")
                cmd("uimenu ingameWpnSelect; wait 30")
                capture("weapons_reference")
                stdin.write("quit\n")
                stdin.flush()
                assert process.wait(timeout=30) == 0
                return 0
            values, powers = state()
            assert values["active"] == "1" and values["campaign"] == "ja" and values["remaining"] == "1", values
            assert powers["sense"][2] == 0 and powers["heal"][2] == 1, powers
            capture("force_4x3")
            click(90, 64)
            values, powers = state()
            assert values["remaining"] == "0" and powers["absorb"][:2] == (2, 1), (values, powers)
            click(90, 118)
            assert state()[1]["heal"][0] == 1, "JA allocated more than one point"
            click(90, 64)
            assert state()[0]["remaining"] == "1", "JA undo failed"
            click(540, 228)
            assert state()[1]["rage"][0] == 2

            for width, height, name in ((1280, 720, "wide"), (1600, 720, "ultrawide"), (600, 800, "portrait")):
                cmd(f"set r_mode -1; set r_customwidth {width}; set r_customheight {height}; vid_restart; wait 60")
                values, powers = state()
                scale = min(width / 640, height / 480)
                x, y, w, h = map(float, values["viewport"].split(","))
                assert abs(w - 640 * scale) < .2 and abs(h - 480 * scale) < .2, values
                assert abs(x - (width - w) / 2) < .2 and abs(y - (height - h) / 2) < .2, values
                assert powers["rage"][:2] == (2, 1) and values["remaining"] == "0", "Video restart lost the allocation"
                image = capture("force_" + name)
                # Check actual artwork placement as well as the reported layout geometry.
                patch = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(image), "-vf",
                    f"crop=20:20:{int(x + 8 * scale)}:{int(y + 8 * scale)}", "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"])
                assert sum(patch[2::3]) / 400 > 30, "Artwork does not match the aspect-safe canvas"
                bar_x, bar_y = (0, height // 2) if x > 20 else (width // 2, 0)
                bar = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(image), "-vf",
                    f"crop=16:16:{bar_x}:{bar_y}", "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"])
                assert max(bar) < 4, "Artwork leaked into the letterbox or pillarbox"
            click(70, 459)
            assert state()[0]["help"] == "1"
            capture("help_star_wars_font")
            key("Return")
            assert state()[0]["help"] == "0"
            click(405, 459)
            assert state()[0]["page"] == "2"
            capture("weapons_empty_portrait")
            click(175, 72)
            click(260, 72)
            click(530, 72)
            assert state()[0]["ready"] == "1"
            capture("weapons_selected_portrait")
            # A third main weapon must not replace either chosen slot.
            click(175, 142)
            assert "selected=3,5,10" in cmd("rml_selection_status")
            cmd("set r_mode 3; vid_restart; wait 60")
            assert "selected=3,5,10" in cmd("rml_selection_status")
            capture("weapons_selected_4x3")
            click(510, 459)
            for _ in range(60):
                result = cmd("wait 10; rml_selection_status; jo_prepare status; campaign_status")
                if "rml_selection active=0" in result and "campaign=ja map=t1_sour" in result: break
            else:
                raise AssertionError("JA Begin Mission did not return to gameplay")
            assert re.search(r"jo_prepare power=rage .*live=2", result), result
            loadout = re.search(r"jo_loadout weapons=(\d+)", result)
            assert loadout and int(loadout[1]) & ((1 << 3) | (1 << 5) | (1 << 10)) == ((1 << 3) | (1 << 5) | (1 << 10)), result
            # Check first-use help, full ranks, and the academy weapon-only mission branch.
            cmd("exitview; wait 60; use end_level; wait 60")
            cmd("setForceAll 3; set weapon_menu 1; uimenu ingameForceSelect")
            values, powers = state()
            assert values["help"] == "1" and values["remaining"] == "0", values
            key("Return")
            click(90, 64)
            assert state()[1]["absorb"][0] == 3, "A full power gained another rank"
            click(405, 459)
            assert state()[0]["help"] == "1"
            key("Escape")
            click(175, 72)
            click(260, 72)
            click(530, 72)
            click(510, 459)
            for _ in range(60):
                result = cmd("wait 10; rml_selection_status; campaign_status")
                if "rml_selection active=0" in result and "campaign=ja map=hoth2" in result: break
            else:
                raise AssertionError("JA academy mission branch did not load Hoth")
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            assert not re.search(r"ERROR:|Unknown command|RmlUi:.*(?:[Ee]rror|Invalid|Failed)", text), log
            print(f"PASS: RmlUi JA selection, one-point allocation, loadout, help, resizing, and mission start ({args.renderer})")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
