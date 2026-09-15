#!/usr/bin/env python3
"""Test one stormtrooper's Jolt reactions in an isolated headless game."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-vanilla")
    parser.add_argument("--projectiles", action="store_true", help="Test automatic reactions through real missile collisions")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 960x720x24",
                                sys.executable, __file__, *sys.argv[1:], "--inside"])
    output = ROOT / "build/jolt-tests"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=args.renderer + ".", dir=output))
    profile, log = run / "profile", run / "console.log"
    print(f"Jolt test: {run}", flush=True)
    env = dict(os.environ, OJK_PROFILE=str(profile), LIBGL_ALWAYS_SOFTWARE="1",
               LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    command = ["bash", str(args.package.resolve() / "launch-sp.sh"),
               os.environ.get("OJK_ASSETS", str(ROOT / "GameData")), "+safe",
               "+set", "cl_renderer", args.renderer, "+set", "r_fullscreen", "0",
               "+set", "r_mode", "-1", "+set", "r_customwidth", "960", "+set", "r_customheight", "720",
               "+set", "s_initsound", "0", "+set", "com_maxfps", "60", "+set", "developer", "0",
               "+set", "con_notifytime", "-1", "+set", "cg_thirdPerson", "0",
               "+set", "g_joltReactions", "0", "+devmap", "t1_sour",
               "+wait", "60", "+exitview", "+god", "+notarget",
               "+wait", "30", "+echo", "JOLT_TEST_READY"]
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream,
                                   stderr=subprocess.STDOUT, env=env, text=True)
        stdin = process.stdin
        assert stdin is not None
        sequence = 0

        def wait_for(marker, start=0):
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")[start:]
                if marker in text:
                    return text
                if process.poll() is not None:
                    raise RuntimeError(f"Game exited with {process.returncode}: {log}")
                time.sleep(0.02)
            raise TimeoutError(log)

        def cmd(text, frames=4):
            nonlocal sequence
            sequence += 1
            marker = f"JOLT_COMMAND_{sequence}_DONE"
            start = len(log.read_text(errors="replace"))
            stdin.write(f"{text}; wait {frames}; echo {marker}\n")
            stdin.flush()
            return re.sub(r"\^[0-9]", "", wait_for(marker, start))

        def status(name="") -> dict[str, Any]:
            line = re.findall(r"jolt actor=[^\r\n]+", cmd("jolt_status " + name, 0))[-1]
            return {k: tuple(map(float, v.split(","))) if "," in v else float(v)
                    for k, v in (word.split("=") for word in line.split()[1:])}

        def magnitude(state):
            return sum(abs(v) for key in ("torso", "head") for v in state[key])

        try:
            wait_for("JOLT_TEST_READY")
            assert status()["actor"] == -1
            if args.projectiles:
                cmd("give weaponnum 3; weapon 3; set d_npcfreeze 1; setviewpos 5504 -4520 64 225; wait 10; npc spawn stormtrooper jolt_projectile_a; wait 30; save jolt_projectiles")
                before = status("jolt_projectile_a")
                cmd("set g_joltReactions 0; jolt_shoot jolt_projectile_a; wait 4")
                vanilla = status("jolt_projectile_a")
                assert 0 < vanilla["health"] < before["health"] and vanilla["hits"] == 0, vanilla
                cmd("load jolt_projectiles; wait 60; set g_joltReactions 1; jolt_shoot jolt_projectile_a; wait 4; screenshot_png projectile_primary")
                first = status("jolt_projectile_a")
                assert first["health"] == vanilla["health"] and first["hits"] == 1 and first["painanim"] == 0, first
                cmd("setviewpos 5504 -4520 64 135; wait 4; npc spawn stormtrooper2 jolt_projectile_b; wait 15")
                assert status("jolt_projectile_b")["health"] > 0
                cmd("jolt_shoot jolt_projectile_b; wait 4; screenshot_png projectile_variant")
                second = status("jolt_projectile_b")
                assert second["hits"] == 1 and second["health"] > 0 and second["painanim"] == 0, second
                assert second["tracked"] >= 2 and status("jolt_projectile_a")["hits"] == 1, second
                cmd("jolt_shoot jolt_projectile_b alt; wait 4; screenshot_png projectile_fatal")
                assert status("jolt_projectile_b")["health"] <= 0
                cmd("setviewpos 5504 -4520 64 45; wait 4; npc spawn stormtrooper jolt_protected; wait 15; nav memory jolt_protected protect; jolt_shoot jolt_protected; wait 4")
                protected = status("jolt_protected")
                assert protected["hits"] == 0 and protected["health"] == before["health"], protected
                cmd("setviewpos 5504 -4520 64 315; wait 4; npc spawn protocol jolt_unsupported; wait 15")
                unsupported = status("jolt_unsupported")
                cmd("jolt_shoot jolt_unsupported; wait 4")
                untouched = status("jolt_unsupported")
                assert untouched["hits"] == 0 and untouched["health"] < unsupported["health"], untouched
                cmd("set g_joltReactions 0; wait 10")
                assert status()["tracked"] == 0
                stdin.write("quit\n")
                stdin.flush()
                assert process.wait(timeout=30) == 0
                assert not re.search(r"Unknown command|trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY", log.read_text(errors="replace"))
                print(f"PASS: {args.renderer}: primary/alt projectiles, automatic multi-actor reactions, unchanged damage, protected/unsupported targets, disable", flush=True)
                return 0
            cmd("give weaponnum 3; weapon 3; set d_npcfreeze 1; setviewpos 5504 -4520 64 45; wait 10; npc spawn stormtrooper jolt_test_actor; wait 30; set g_joltReactions 1; jolt_select jolt_test_actor")
            cmd("set cg_thirdPerson 1; set cg_thirdPersonRange 160; set cg_thirdPersonAngle 30; wait 10")
            initial = status()
            if initial["active"] != 1:
                cmd("jolt_status jolt_test_actor; ui_report; viewpos; screenshot_png jolt_setup")
            assert initial["actor"] > 0 and initial["active"] == 1, initial
            cmd("set g_joltReactionPose 0")
            baseline = status()
            cmd("jolt_impulse left; wait 1; screenshot_png jolt_animation_only; wait 50")
            assert status()["poses"] == baseline["poses"]
            assert status()["health"] == baseline["health"]
            cmd("set g_joltReactionPose 1; jolt_impulse left; wait 1; screenshot_png jolt_physical_reaction; wait 50")
            assert status()["poses"] > baseline["poses"]
            cmd("jolt_select jolt_test_actor; wait 4")
            initial = status()
            cmd("screenshot_png jolt_neutral; save jolt_neutral")
            cmd("jolt_hit front; wait 3; screenshot_png jolt_front")
            hit = status()
            assert hit["hits"] == 1 and hit["poses"] > initial["poses"] and hit["peak"] > 0.5, hit
            assert hit["health"] == initial["health"] - 5, (initial, hit)
            assert hit["painanim"] == 0, hit
            cmd("wait 50")
            assert magnitude(status()) < 0.5
            cmd("jolt_hit left; wait 1; screenshot_png jolt_left; wait 50")
            assert status()["hits"] == 2
            cmd("jolt_hit head; wait 1; screenshot_png jolt_head; wait 50")
            assert status()["hits"] == 3
            assert magnitude(status()) < 0.5
            cmd("set d_npcfreeze 0; set g_joltDebug 1; jolt_knockdown; wait 4; screenshot_png jolt_fall")
            assert status()["falling"] == 1
            cmd("wait 20; screenshot_png jolt_ground; wait 100")
            recovered = status()
            assert recovered["active"] == 1 and magnitude(recovered) < 0.2, recovered
            assert abs(recovered["recovery_lift"]) <= 8, recovered
            assert recovered["pose_error"] < 0.1, recovered
            cmd("jolt_control")
            for direction in ("back", "moveleft", "moveright", "forward"):
                cmd(f"+{direction}; wait 3")
                if status()["movement"] >= 180:
                    cmd(f"jolt_impulse left; -{direction}; wait 2; screenshot_png jolt_running_fall")
                    break
                cmd(f"-{direction}")
            else:
                raise AssertionError("No clear running direction for the controlled actor")
            running = status()
            assert running["falling"] == 1 and running["launch"] > 80, running
            assert running["pose_error"] < .1, running
            assert running["health"] == recovered["health"], running
            cmd("wait 120")
            assert status()["falling"] == 0
            cmd("set d_npcfreeze 1")
            cmd("set timescale 0.25")
            start = status()
            before = time.monotonic()
            time.sleep(1)
            slow = status()
            rate = (slow["steps"] - start["steps"]) / (time.monotonic() - before)
            assert 15 < rate < 45, rate
            cmd("set timescale 1; set g_joltReactions 0; wait 10")
            assert status()["actor"] == -1
            cmd("set g_joltReactions 1; jolt_select jolt_test_actor; jolt_hit front; load jolt_neutral; wait 60")
            assert status()["actor"] == -1  # Transient rig state does not survive saves.
            cmd("set g_joltReactions 1; jolt_select jolt_test_actor; vid_restart; wait 60")
            assert status()["actor"] == -1  # Game reload and renderer shutdown release the rig.
            cmd("jolt_select jolt_test_actor; jolt_knockdown; wait 2; save jolt_midfall; wait 4")
            assert status()["actor"] == -1  # Save contains a normal hull and no physics overrides.
            cmd("load jolt_midfall; wait 60")
            assert status()["actor"] == -1
            cmd("jolt_select jolt_test_actor; npc kill jolt_test_actor; wait 30")
            assert status()["actor"] == -1
            cmd("npc spawn protocol jolt_other_actor; wait 30; jolt_select jolt_other_actor")
            assert status()["actor"] == -1
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            assert not re.search(r"Unknown command|trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY", text), log
            print(f"PASS: {args.renderer}: localized hits, recovery, damage, slow time, disable, load, restart, death, shutdown", flush=True)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            stdin.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
