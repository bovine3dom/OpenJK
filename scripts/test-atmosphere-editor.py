#!/usr/bin/env python3
"""Exercise the RmlUi atmosphere editor with real mouse input and an X11 clipboard."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time

from atmosphere_profiles import read_profile


def clipboard():
    return subprocess.check_output(["xclip", "-selection", "clipboard", "-out"], text=True, timeout=10)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 960x720x24", sys.executable, __file__,
                                "--inside", "--package", str(args.package)])
    package = args.package.resolve()
    suite = Path(tempfile.mkdtemp(prefix="atmosphere-editor.", dir=root / "build/smoke"))
    profile = suite / "profile/OpenJK"
    profile.mkdir(parents=True)
    settings = dict(cl_renderer="rdsp-rend2", r_mode=-1, r_customwidth=960, r_customheight=720,
                    r_fullscreen=0, in_nograb=1, s_initsound=0, developer=1, com_maxfps=30,
                    r_ignoreGLErrors=0, r_autoExposure=0, r_dynamicGlow=1,
                    r_ssao=0, r_sss=0, r_capsuleShadows=0, r_smaa=0)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("// Controlled atmosphere editor fixture.\n")
    env = dict(os.environ, OJK_PROFILE=str(profile.parent), LIBGL_ALWAYS_SOFTWARE="1",
               LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    log = suite / "console.log"
    source = package / "OpenJK/maps/t1_sour.atmosphere"
    original = source.read_bytes()
    original_hash = hashlib.sha256(original).hexdigest()
    print(f"Atmosphere editor results: {suite}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(["bash", str(package / "launch-sp.sh"), str(root / "GameData"),
            "+devmap", "t1_sour", "+wait", "100", "+echo", "EDITOR_READY"], env=env,
            stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT, text=True, start_new_session=True)
        assert process.stdin is not None
        stdin = process.stdin
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
            marker = f"EDITOR_CMD_{serial}_DONE"
            start = len(text())
            stdin.write(f"{command}; wait 3; echo {marker}\n")
            stdin.flush()
            return wait(marker, start)
        def xdo(*arguments):
            return subprocess.check_output(["xdotool", *map(str, arguments)], text=True).strip()
        def rect(name):
            result = cmd("atmosphere_editor_status " + name)
            match = re.search(r"rect=([\d.,-]+)", result)
            assert match, result
            return tuple(map(float, match[1].split(',')))
        def move(x, y):
            for _ in range(2):
                xdo("mousemove", "--window", window, 958, 718); cmd("wait 2")
                xdo("mousemove", "--window", window, 1, 1); cmd("wait 2")
            xdo("mousemove_relative", "--", round(x)-1, round(y)-1)
            cmd("wait 2")
        def click(name):
            x, y, w, h = rect(name)
            move(x+w/2, y+h/2)
            xdo("click", 1); cmd("wait 5")
        def show(name):
            form = rect("form")
            move(form[0]+form[2]-20, form[1]+form[3]/2)
            for _ in range(40):
                box = rect(name)
                if form[1]+10 < box[1] and box[1]+box[3] < form[1]+form[3]-10:
                    return
                xdo("click", 5 if box[1]+box[3] >= form[1]+form[3]-10 else 4)
                cmd("wait 3")
            raise RuntimeError(f"Could not scroll to {name}")
        def capture(name):
            cmd(f"screenshot_png {name}; wait 20")
            return subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(profile / "screenshots" / f"{name}.png"),
                "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"])
        def enter(name, value):
            click(name)
            xdo("key", "--clearmodifiers", "ctrl+a")
            xdo("type", "--clearmodifiers", "--delay", 30, value)
            cmd("wait 5")
        def player():
            match = re.search(r"playerstate [^\n]+", cmd("nav player"))
            assert match
            return match[0]
        try:
            wait("EDITOR_READY")
            window = xdo("search", "--onlyvisible", "--pid", process.pid).splitlines()[-1]
            xdo("windowfocus", "--sync", window)
            cmd("exitview; wait 100; god; give all; wait 40; weapon 3; wait 80")
            cmd("noclip; d_npcfreeze 1; cg_draw2D 0; cg_drawGun 0; r_drawentities 0; con_notifytime -1")
            cmd("setviewpos 4693 -1624 380 45; wait 60; r_atmosphere 1")
            before = player()
            cmd("atmosphere_editor")
            state = cmd("atmosphere_editor_status")
            assert "active=1" in state and "paused=1" in state and "scripts/maps/shared/ja-tatooine.atmosphere" in state, state
            capture("opened")
            click("help_16")
            capture("help")
            show("n_10_0")
            # Exercise the slider before using exact text entry.
            x, y, w, h = rect("s_10_0")
            move(x+w*.5, y+h*.5)
            xdo("mousedown", 1)
            xdo("mousemove_relative", "--", 35, 0); cmd("wait 5")
            xdo("mouseup", 1); cmd("wait 5")
            slider = cmd("atmosphere_editor_status")
            match = re.search(r"draft_light=([\d.e+-]+) applied_light=([\d.e+-]+)", slider)
            assert match and match[1] != match[2], slider
            enter("n_10_0", "0.1")
            click("apply")
            assert "applied_light=0.1" in cmd("atmosphere_editor_status")
            changed = capture("applied")
            xdo("key", "w"); cmd("wait 5")
            assert before == player(), "Editor input changed the player"
            click("toggle")
            assert 'enabled=0' in cmd("atmosphere_editor_status")
            stock = capture("stock")
            delta = sum(abs(changed[(y*960+x)*3+c] - stock[(y*960+x)*3+c])
                        for y in range(40, 260) for x in range(540, 930) for c in range(3)) / (220*390*3)
            assert delta > 1, delta
            click("copy")
            exported = clipboard()
            assert exported.startswith("// File: scripts/maps/shared/ja-tatooine.atmosphere\n"), exported
            export = suite / "clipboard.atmosphere"
            export.write_text(exported)
            assert read_profile(export, "textures/skies/desert")[1]["illuminance"] == [.1]
            enter("n_10_0", "nan")
            click("apply")
            assert "applied_light=0.1" in cmd("atmosphere_editor_status")
            assert "must be between" in cmd("atmosphere_editor_status status")
            click("reload")
            assert "applied_light=16" in cmd("atmosphere_editor_status")
            show("compass"); capture("sun_compass")
            show("ray_5"); capture("density")
            xdo("key", "Escape"); cmd("wait 5")
            assert "active=0" in cmd("atmosphere_editor_status")
            assert re.search(r'cl_paused.*"0"', cmd("cl_paused"))
            assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
            # A regular local override must export to the map file, not the shared default.
            (profile / "maps").mkdir()
            private = profile / "maps/t1_sour.atmosphere"
            private.write_bytes(original)
            cmd("r_atmosphereReload; atmosphere_editor")
            click("copy")
            assert clipboard().startswith("// File: scripts/maps/t1_sour.atmosphere\n")
            assert private.read_bytes() == original
            cmd("vid_restart; wait 150")
            assert "active=0" in cmd("atmosphere_editor_status")
            cmd("atmosphere_editor; wait 5; devmap t2_wedge; wait 100")
            assert "active=0" in cmd("atmosphere_editor_status")
            # Krildor starts a video which legitimately owns a pause of its own.
            time.sleep(2.5)  # The movie ignores very early skip input.
            window = xdo("search", "--onlyvisible", "--pid", process.pid).splitlines()[-1]
            xdo("windowfocus", "--sync", window)
            xdo("key", "space"); cmd("wait 10")
            assert re.search(r'cl_paused.*"0"', cmd("cl_paused"))
            stdin.write("quit\n"); stdin.flush()
            process.wait(timeout=20)
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=10)
    output = text()
    assert not re.search(r"GL_INVALID_|GL_OUT_OF_MEMORY|Unknown command|trying to load fallback|"
                         r"RmlUi:.*(?:ERROR|Syntax error|Invalid property|Failed)", output), log
    (suite / "result.json").write_text(json.dumps(dict(sky_delta=delta, clipboard=exported), indent=2) + "\n")
    print("PASS: real sliders, numeric input, help, apply/toggle, clipboard, file routing, input capture, and lifecycle")


if __name__ == "__main__":
    sys.exit(main())
