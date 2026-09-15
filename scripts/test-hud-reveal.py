#!/usr/bin/env python3
"""Check held HUD visibility, ally bearings, and input cleanup headlessly."""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-rend2", "rdsp-vanilla"), default="rdsp-rend2")
    parser.add_argument("--campaign", choices=("ja", "jo"), default="ja")
    parser.add_argument("--width", type=int, choices=(960, 1280), default=1280)
    args = parser.parse_args()
    run = Path(tempfile.mkdtemp(prefix="hud-reveal.", dir=root / "build/smoke"))
    home = run / "profile"
    profile = home / ("campaigns/jo/OpenJK" if args.campaign == "jo" else "OpenJK")
    profile.mkdir(parents=True)
    settings = dict(cl_renderer=args.renderer, r_mode=-1, r_customwidth=args.width, r_customheight=720,
                    r_fullscreen=0, s_initsound=0, developer=1, com_maxfps=60, cg_dynamicCrosshair=0)
    (profile / "openjk_sp.cfg").write_text("".join(f'set {k} "{v}"\n' for k, v in settings.items()))
    (profile / "autoexec_sp.cfg").write_text("")
    env = dict(os.environ, OJK_PROFILE=str(home), SDL_AUDIODRIVER="dummy",
               OJK_JO_ASSETS=os.environ.get("OJK_JO_ASSETS", str(root / "GameData_JO")))
    command = ["bash", str(args.package.resolve() / "launch-sp.sh"), str(root / "GameData"),
               "--campaign", args.campaign, "+devmap", "kejim_post" if args.campaign == "jo" else "t2_wedge",
               "+wait", "150", "+echo", "HUD_READY"]
    if args.renderer == "rdsp-rend2":
        env.update(SDL_VIDEODRIVER="offscreen", EGL_PLATFORM="surfaceless")
        env.pop("LIBGL_ALWAYS_SOFTWARE", None)
    else:
        env.update(LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="1")
        command = ["xvfb-run", "-a", "-s", f"-screen 0 {args.width}x720x24", *command]
    log = run / "console.log"
    images, records = {}, {}
    print(f"HUD reveal results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(command, env=env, stdin=subprocess.PIPE, stdout=stream,
                                   stderr=subprocess.STDOUT, text=True, start_new_session=True)
        assert process.stdin is not None
        stdin = process.stdin
        serial = 0

        def text():
            return re.sub(r"\^[0-9]", "", log.read_text(errors="replace"))

        def wait_for(marker, start=0):
            deadline = time.monotonic() + 300
            while time.monotonic() < deadline:
                output = text()[start:]
                if marker in output:
                    return output
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited: {log}")
                time.sleep(0.05)
            raise TimeoutError(f"Missing {marker}: {log}")

        def cmd(commands):
            nonlocal serial
            serial += 1
            marker = f"HUD_COMMAND_{serial}_DONE"
            start = len(text())
            line = f"{commands}; wait 20; echo {marker}\n"
            assert len(line) < 256
            stdin.write(line)
            stdin.flush()
            return wait_for(marker, start)

        def status(name, active):
            output = cmd("hud_status")
            entries = [dict(word.split("=", 1) for word in line.split()[1:] if "=" in word)
                       for line in re.findall(r"hud (?:reveal|ally)=[^\n]+", output)]
            assert entries and entries[0]["reveal"] == entries[0]["compass"] == str(active), output
            records[name] = entries
            return entries[1:]

        def capture(name):
            cmd(f"screenshot_png {name}; wait 20")
            images[name] = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(profile / "screenshots" / f"{name}.png"),
                                                    "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "-"])
            assert len(images[name]) == args.width * 720 * 3

        try:
            wait_for("HUD_READY")
            assert 'Bind V = "+showhud"' in cmd("bind v")
            cmd("exitview; wait 200; helpusobi 1; god; notarget; d_npcfreeze 1; con_notifytime -1")
            cmd("fixedtime 50; give all; wait 150; weapon 3; wait 150; noclip")
            position = (400, -2193, 0, 322) if args.campaign == "jo" else (2688, 640, -60, 315)
            x, y, z, yaw = position
            cmd(f"setviewpos {x} {y} {z} 270; wait 60; d_npcfreeze 0; npc spawn jedi hud_ally; wait 20; d_npcfreeze 1")
            cmd(f"setviewpos {x} {y} {z} {yaw}; wait 60; fixedtime 1")
            time.sleep(6.5)  # Resource activity uses real time, not fixed game time.
            status("idle", 0)
            capture("idle")
            cmd("+showhud 118")
            allies = status("held", 1)
            if not any(a.get("name") == "hud_ally" for a in allies):
                print(cmd("nav actors; nav memory hud_ally"), flush=True)
            ally = next(a for a in allies if a.get("name") == "hud_ally")
            assert all(a["type"] != "stormtrooper" for a in allies), allies
            capture("held")
            cmd("+showhud 120; -showhud 118")
            status("second_key", 1)
            cmd("-showhud 120")
            status("released", 0)
            capture("released")
            cmd("+showhud 118; toggleconsole")
            status("console", 0)
            cmd("toggleconsole")
            status("after_console", 0)
            cmd("cg_draw2D 0; cg_drawHUD 0; cg_drawStatus 0; +showhud 118")
            status("override_hidden", 1)
            capture("override_hidden")
            cmd("-showhud 118")
            status("hidden_restored", 0)
            output = cmd("cg_draw2D; cg_drawHUD; cg_drawStatus")
            assert all(f'{key} = "0"' in output for key in ("cg_draw2D", "cg_drawHUD", "cg_drawStatus"))
            cmd("cg_draw2D 1; cg_drawHUD 1; cg_drawStatus 1; +showhud 118")
            behind = (yaw - float(ally["offset"]) + 180) % 360
            cmd(f"fixedtime 50; setviewpos {x} {y} {z} {behind}; wait 60; fixedtime 1")
            target = next(a for a in status("behind", 1) if a.get("name") == "hud_ally")
            assert target["edge"] != "0", target
            capture("behind")
            cmd(f"fixedtime 50; setviewpos {x} {y} {z + 160} {yaw}; wait 60; fixedtime 1")
            target = next(a for a in status("below", 1) if a.get("name") == "hud_ally")
            assert target["elevation"] == "-1", target
            cmd("save hud_reveal; npc kill hud_ally; wait 100")
            assert not any(a.get("name") == "hud_ally" for a in status("dead_ally", 1))
            cmd("load hud_reveal; wait 150")
            status("loaded", 0)
            cmd("+showhud 118")
            assert any(a.get("name") == "hud_ally" for a in status("restored_ally", 1))
            cmd("vid_restart; wait 150")
            status("restarted", 0)
            cmd("+showhud 118; fixedtime 50")
            before = re.search(r"origin=([^\n]+)", cmd("campaign_status"))
            cmd("+forward; wait 20; -forward")
            after = re.search(r"origin=([^\n]+)", cmd("campaign_status"))
            assert before and after and before[1] != after[1]
            status("movement", 1)
            cmd("-showhud 118")
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=15)
            (run / "states.json").write_text(json.dumps(records, indent=2))

    def changed(a, b, x1, y1, x2, y2):
        return sum(max(abs(images[a][i+c] - images[b][i+c]) for c in range(3)) > 30
                   for y in range(y1, y2) for x in range(x1, x2) for i in [(y * args.width + x) * 3])
    middle = args.width // 2
    assert changed("held", "idle", middle-185, 20, middle+185, 75) > 100, "Compass was not drawn"
    assert changed("held", "idle", middle-32, 328, middle+32, 392) > 40, "Resource indicators were not revealed"
    assert changed("held", "idle", 0, 570, args.width, 720) > 200, "Status panels were not revealed"
    assert changed("held", "released", middle-185, 20, middle+185, 75) > 100, "Compass stayed visible"
    assert not re.search(r"ERROR:|Error:|Unknown command|trying to load fallback", text()), log
    print("PASS: held HUD, ally compass, input cleanup, movement, and save/load")


if __name__ == "__main__":
    main()
