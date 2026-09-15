#!/usr/bin/env python3
"""Check the weapon torch and its shadows with headless hardware rendering."""

import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--campaign", choices=("ja", "jo"), default="ja")
    parser.add_argument("--msaa", type=int, choices=(0, 4), default=0)
    args = parser.parse_args()
    map_name = "kejim_post" if args.campaign == "jo" else "t2_wedge"
    view = "400 -2193 0 322" if args.campaign == "jo" else "2688 640 -60 315"
    _, _, view_z, yaw = map(float, view.split())
    run = Path(tempfile.mkdtemp(prefix="torch.", dir=root / "build/smoke"))
    home = run / "profile"
    profile = home / ("campaigns/jo/OpenJK" if args.campaign == "jo" else "OpenJK")
    profile.mkdir(parents=True)
    settings = dict(r_mode=-1, r_customwidth=960, r_customheight=720, r_fullscreen=0,
                    s_initsound=0, developer=1, r_debugContext=1, r_ignoreGLErrors=0,
                    r_ext_multisample=args.msaa, com_maxfps=60)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {key} "{value}"\n' for key, value in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("")
    (profile / "shaders").mkdir()
    (profile / "shaders/torch_test.shader").write_text("torch/probe { { map $whiteimage } }\n")
    env = dict(os.environ, OJK_PROFILE=str(home), SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless",
               SDL_AUDIODRIVER="dummy", OJK_JO_ASSETS=os.environ.get("OJK_JO_ASSETS", str(root / "GameData_JO")))
    env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    log = run / "console.log"
    images, states = {}, {}
    print(f"Torch results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(["bash", str(args.package.resolve() / "launch-sp.sh"),
                                    os.environ.get("OJK_ASSETS", str(root / "GameData")), "--campaign", args.campaign,
                                    "+devmap", map_name, "+wait", "150", "+echo", "TORCH_READY"],
                                   env=env, stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT, text=True)
        serial = 0
        assert process.stdin is not None
        stdin = process.stdin

        def wait_for(marker, start=0):
            deadline = time.monotonic() + 240
            while time.monotonic() < deadline:
                text = re.sub(r"\^[0-9]", "", log.read_text(errors="replace"))[start:]
                if marker in text:
                    return text
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited: {log}")
                time.sleep(0.05)
            raise TimeoutError(f"Missing {marker}: {log}")

        def cmd(commands):
            nonlocal serial
            serial += 1
            marker = f"TORCH_COMMAND_{serial}_DONE"
            start = len(re.sub(r"\^[0-9]", "", log.read_text(errors="replace")))
            line = f"{commands}; wait 10; echo {marker}\n"
            assert len(line) < 256, "Console input line is too long"
            stdin.write(line)
            stdin.flush()
            return wait_for(marker, start)

        def status(name):
            text = cmd("torch_status")
            match = re.search(r"torch enabled=[^\n]+", text)
            assert match, text
            states[name] = dict(word.split("=", 1) for word in match[0].split()[1:])
            return states[name]

        def capture(name):
            cmd(f"wait 30; screenshot_png {name}; wait 20")
            path = profile / "screenshots" / f"{name}.png"
            images[name] = subprocess.check_output(["ffmpeg", "-v", "error", "-xerror", "-i", str(path),
                                                    "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"])
            assert len(images[name]) == 960 * 720 * 3

        try:
            wait_for("TORCH_READY")
            assert 'Bind L = "torch"' in cmd("bind l")
            if args.campaign == "jo":
                assert "camera=1" in cmd("campaign_status")
                cmd("cg_torch 1; wait 30")
                assert status("cinematic")["enabled"] == "1" and states["cinematic"]["active"] == "0"
                cmd("cg_torch 0")
            cmd(f"exitview; wait {200 if args.campaign == 'jo' else 0}; helpusobi 1; god; d_npcfreeze 1; cg_draw2D 0; con_notifytime -1")
            cmd("r_autoExposure 0; r_ssao 0; r_capsuleShadows 0; r_sss 0; r_dynamicGlow 0; r_dynamiclight 0")
            cmd("cg_shadows 0; cg_thirdPerson 0; fixedtime 50; give all; wait 150")
            if args.campaign == "jo":
                cmd("give weaponnum 18; wait 30")
            # Noclip skips weapon handling. Finish the weapon change before enabling it.
            cmd(f"weapon {18 if args.campaign == 'jo' else 3}; wait 150; noclip; setviewpos {view}; wait 150; fx_freeze 1; fixedtime 1")
            assert status("off")["enabled"] == "0"
            capture("off")
            cmd("torch")
            assert status("on")["active"] == "1" and states["on"]["mount"] == "weapon"
            capture("on")
            cmd("r_torchShadows 0")
            capture("unshadowed")
            cmd("r_torchShadows 1")
            capture("shadow_restored")
            cmd("torch")
            assert status("off_again")["active"] == "0"
            capture("off_restored")
            cmd("torch; cg_drawGun 0")
            assert status("hidden_gun")["active"] == "1" and states["hidden_gun"]["mount"] == "view"
            cmd(f"cg_drawGun 1; fixedtime 50; noclip; weapon 4; wait 150; noclip; setviewpos {view}; wait 60; fixedtime 1")
            assert status("disruptor")["mount"] == "weapon"
            assert math.dist(*(tuple(map(float, states[key]["origin"].split(","))) for key in ("on", "disruptor"))) > 1
            text = cmd("testparticle torch/probe 1 0; testparticle")
            hit = re.search(r"Test particle: ([-\d.]+) ([-\d.]+) ([-\d.]+)", text)
            assert hit, text
            x, y = float(hit[1]) - 4 * math.cos(math.radians(yaw)), float(hit[2]) - 4 * math.sin(math.radians(yaw))
            cmd(f"fixedtime 50; setviewpos {x} {y} {view_z} {yaw}; wait 60; fixedtime 1")
            assert status("wall")["clipped"] == "1" and states["wall"]["active"] == "1"
            capture("wall")
            cmd(f"setviewpos {view}; cg_thirdPerson 1; fixedtime 0; wait 150")
            assert status("third_person")["active"] == "1" and states["third_person"]["thirdperson"] == "1"
            capture("third_person")
            cmd("cg_thirdPerson 0; r_sss 1; r_capsuleShadows 1; r_ssao 1; wait 60; save torch_test; vid_restart; wait 150")
            assert status("restarted")["active"] == "1"
            capture("restarted")
            cmd("load torch_test; wait 150")
            assert status("loaded")["active"] == "1"
            capture("loaded")
            if args.campaign == "ja":
                cmd("devmap t1_sour; wait 150")
                assert status("cinematic")["enabled"] == "1" and states["cinematic"]["active"] == "0"
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    def delta(a, b):
        return sum(abs(x-y) for x, y in zip(images[a], images[b])) / len(images[a])
    # The nearby stormtrooper casts onto this wall. Exclude animated sky and gun pixels.
    x1, y1, x2, y2 = (560, 390, 690, 495) if args.campaign == "jo" else (430, 420, 550, 530)
    wall = [(y * 960 + x) * 3 + c for y in range(y1, y2) for x in range(x1, x2) for c in range(3)]
    metrics = dict(effect=delta("off", "on"), restore=delta("off", "off_restored"),
                   shadow=sum(max(0, images["unshadowed"][i] - images["on"][i]) for i in wall) / len(wall),
                   shadow_restore=sum(abs(images["on"][i] - images["shadow_restored"][i]) for i in wall) / len(wall))
    results = dict(states=states, **metrics)
    (run / "results.json").write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))
    text = log.read_text(errors="replace")
    assert "----- rdsp-rend2 -----" in text and not re.search(r"llvmpipe|softpipe|GL_INVALID_|GL_OUT_OF_MEMORY|trying to load fallback|ERROR:", text, re.I)
    assert metrics["effect"] > max(0.2, 4 * metrics["restore"]), "Torch did not light the scene"
    assert metrics["shadow"] > max(0.5, 3 * metrics["shadow_restore"]), "Torch shadows did not occlude light"
    print("PASS: torch lighting, shadows, attachment, clipping, and lifecycle")


if __name__ == "__main__":
    main()
