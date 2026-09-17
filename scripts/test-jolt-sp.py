#!/usr/bin/env python3
"""Test one stormtrooper's Jolt reactions in an isolated headless game."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import signal
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
    parser.add_argument("--control", action="store_true", help="Test the motor-driven balance controller")
    parser.add_argument("--floor-combat", action="store_true", help="Test movement over fallen NPCs and the saber floor finisher")
    parser.add_argument("--collapse", action="store_true", help="Test and record the motor fade during a standing death")
    parser.add_argument("--force-effects", action="store_true", help="Test native Grip and Lightning with physical reactions")
    parser.add_argument("--force-effect", choices=("grip", "lightning"), help="Test only one Force effect")
    parser.add_argument("--gameplay", action="store_true", help="Test humanoids, ten active rigs, explosions, and corpse continuity")
    parser.add_argument("--rig-types", nargs="+", help="NPC types for the gameplay rig test (maximum 16)")
    parser.add_argument("--demo", action="store_true", help="Test each demonstration case and save motion samples")
    parser.add_argument("--demo-case", action="append", choices=("idle", "hit", "step", "leg", "run", "fall"))
    parser.add_argument("--push-speed", type=float, default=1.3, help="Demo push velocity change in metres/second")
    parser.add_argument("--debug", action="store_true", help="Draw the physical skeleton and log the captured rig")
    parser.add_argument("--launcher", action="store_true", help="Start through --jolt-demo and the packaged configuration")
    parser.add_argument("--record", action="store_true", help="Record the Xvfb display to motion.mp4 with ffmpeg")
    parser.add_argument("--fps", type=int, choices=(60, 120, 144), default=60)
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    args.force_effects = args.force_effects or bool(args.force_effect)
    args.demo = args.demo or bool(args.demo_case) or args.launcher
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
               os.environ.get("OJK_ASSETS", str(ROOT / "GameData")),
               *(["--jolt-demo"] if args.launcher else []), "+safe",
               "+set", "cl_renderer", args.renderer, "+set", "r_fullscreen", "0",
               "+set", "r_mode", "-1", "+set", "r_customwidth", "960", "+set", "r_customheight", "720",
                "+set", "s_initsound", "0", "+set", "com_maxfps", str(args.fps), "+set", "developer", "0",
                "+set", "con_notifytime", "-1", "+set", "cg_thirdPerson", "0"]
    if not args.launcher:
        command += ["+set", "g_joltReactions", "0", "+devmap", "t1_sour",
                "+wait", "60", "+exitview", "+god", "+notarget",
                "+wait", "30"]
    command += ["+echo", "JOLT_TEST_READY"]
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream,
                                   stderr=subprocess.STDOUT, env=env, text=True)
        stdin = process.stdin
        assert stdin is not None
        sequence = 0
        recorder = None

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
            script = f"{text}; wait {frames}; echo {marker}\n"
            if len(script) > 200:
                # Engine stdin has a small line buffer. Execute long atomic batches from a file.
                folder = profile / ("jolt-demo/OpenJK" if args.launcher else "OpenJK")
                folder.mkdir(parents=True, exist_ok=True)
                name = f"jolt_command_{sequence}.cfg"
                (folder / name).write_text(script)
                script = f"exec {name}\n"
            stdin.write(script)
            stdin.flush()
            return re.sub(r"\^[0-9]", "", wait_for(marker, start))

        def status(name="") -> dict[str, Any]:
            text = cmd("jolt_status " + name, 0)
            line = re.findall(r"jolt actor=[^\r\n]+", text)[-1]
            support = re.findall(r"jolt support ([^\r\n]+)", text)
            if support:
                line += " " + support[-1]
            recovery = re.findall(r"jolt recovery ([^\r\n]+)", text)
            if recovery:
                line += " " + recovery[-1]
            ownership = re.findall(r"jolt ownership ([^\r\n]+)", text)
            if ownership:
                line += " " + ownership[-1]
            effects = re.findall(r"jolt effects ([^\r\n]+)", text)
            if effects:
                line += " " + effects[-1]
            facing = re.findall(r"jolt facing ([^\r\n]+)", text)
            if facing:
                line += " " + facing[-1]
            combat = re.findall(r"jolt combat ([^\r\n]+)", text)
            if combat:
                line += " " + combat[-1]
            return {k: tuple(map(float, v.split(","))) if "," in v else float(v)
                    for k, v in (word.split("=") for word in line.split()[1:])}

        def magnitude(state):
            return sum(abs(v) for key in ("torso", "head") for v in state[key])

        try:
            wait_for("JOLT_TEST_READY")
            if args.launcher:
                cmd("wait 20; jolt_demo stop")
            else:
                assert status()["actor"] == -1
            if args.record:
                recorder = subprocess.Popen(["ffmpeg", "-nostdin", "-loglevel", "error", "-y",
                    "-f", "x11grab", "-framerate", "30", "-video_size", "960x720", "-i", env["DISPLAY"],
                    "-c:v", "libx264", "-threads", "1", "-preset", "ultrafast", "-crf", "22",
                    str(run / "motion.mp4")], stdout=subprocess.DEVNULL, stderr=stream)
            if args.force_effects:
                results = {}
                for power in (() if args.force_effect == "lightning" else (1, 2, 3)):
                    start = len(log.read_text(errors="replace"))
                    cmd("jolt_demo idle")
                    wait_for("Jolt demo finished: idle", start)
                    before = status("jolt_demo_actor")
                    cmd(f"setforcegrip {power}; give force; campaign_status; +force_grip", 0)
                    samples = []
                    for _ in range(12):
                        samples.append(status("jolt_demo_actor"))
                        cmd("wait 1", 0)
                    if power > 1:
                        # A struggle can briefly rotate the torso; the held heading must stay near the caster.
                        assert max(abs(s["yaw_error"]) for s in samples[-3:]) < 10, samples[-3:]
                        assert max(abs(s["torque"]) for s in samples) <= 39.1, samples[-1]
                    if power == 2:
                        x, y, z = samples[-1]["player_origin"]
                        cmd(f"give force; setviewpos {x+64} {y} {z+25} 117; wait 10")
                        turned = status("jolt_demo_actor")
                        assert turned["grip"] == 2 and abs(turned["yaw_error"]) < 15, turned
                        assert abs((turned["facing_yaw"]-samples[-1]["facing_yaw"]+180) % 360-180) > 10, turned
                    if power == 3:
                        x, y, z = samples[-1]["player_origin"]
                        cmd(f"give force; setviewpos {x} {y} {z+25} 105; wait 10")
                        carried = status("jolt_demo_actor")
                        assert carried["grip"] == 3 and abs(carried["origin"][0]-samples[-1]["origin"][0]) > 5, carried
                        cmd("set g_joltReactions 0; wait 4")
                        assert status("jolt_demo_actor")["grip"] == 0
                        cmd("set g_joltReactions 1; give force; wait 6")
                        assert status("jolt_demo_actor")["grip"] == 3
                        cmd("save jolt_grip; load jolt_grip; wait 10; give force")
                        loaded = status("jolt_demo_actor")
                        assert loaded["grip"] == 3 and abs(loaded["yaw_error"]) < 20, loaded
                        cmd("npc kill jolt_demo_actor; wait 4")
                        dead = status("jolt_demo_actor")
                        assert dead["corpse"] and dead["grip"] == 3 and dead["engaged"], dead
                    cmd(f"-force_grip; wait {70 if power == 1 else 4}")
                    released = status("jolt_demo_actor")
                    results[f"grip{power}"] = samples
                    (run / "force-results.json").write_text(json.dumps(results, indent=2))
                    assert any(s["grip"] == power for s in samples), samples[:3]
                    assert released["grip"] == 0, released
                    assert released["torque"] == 0, released
                    if power == 1:
                        held = [s for s in samples if s["grip"] == 1]
                        assert min(s["pelvis_z"] for s in held) > before["pelvis_z"]-12, held[-1]
                        assert max(s["grip_error"] for s in held) < .25, held[-1]
                        assert not any(s["legs_passive"] for s in held), held[-1]
                    else:
                        assert max(s["grip_force"] for s in samples) > 100, samples
                        assert max(s["grip_struggles"] for s in samples) > 0, samples
                        assert all(s["legs_passive"] for s in samples if s["grip"] == power), samples[-1]
                        assert 0 < max(s["leg_tone"] for s in samples) <= 5.01, samples[-1]
                    if power == 2:
                        assert max(s["pelvis_z"] for s in samples) > before["pelvis_z"]+12, samples[-1]
                    print(f"PASS: {args.renderer}: Grip level {power} and release", flush=True)
                for power in (() if args.force_effect == "grip" else (1, 2, 3)):
                    start = len(log.read_text(errors="replace"))
                    cmd("jolt_demo idle")
                    wait_for("Jolt demo finished: idle", start)
                    before = status("jolt_demo_actor")
                    cmd(f"setforcelightning {power}; set g_joltLightningPushScale .5; give force; +force_lightning", 0)
                    samples = []
                    for _ in range(12):
                        samples.append(status("jolt_demo_actor"))
                        cmd("wait 1", 0)
                    cmd("-force_lightning; wait 20")
                    stopped = status("jolt_demo_actor")
                    results[f"lightning{power}"] = samples
                    (run / "force-results.json").write_text(json.dumps(results, indent=2))
                    assert any(s["shock"] > 0 and s["hits"] > 0 for s in samples), samples[:3]
                    assert stopped["health"] < before["health"] and stopped["shock"] == 0, stopped
                    limit = 5.08/(3 if power == 1 else 1)
                    assert max(s["shock_push"] for s in samples) > 0, samples[-1]
                    assert max(s["shock_rate"] for s in samples) <= limit+.01, samples[-1]
                    if power == 3:
                        assert max(s["shock_push"] for s in samples) > limit*2, samples[-1]
                    assert max(s["pelvis_z"] for s in samples) < before["pelvis_z"]+16, samples[-1]
                    print(f"PASS: {args.renderer}: Lightning {power}, continuous push, contractions, and fade", flush=True)
                if args.force_effect != "grip":
                    start = len(log.read_text(errors="replace"))
                    cmd("jolt_demo idle")
                    wait_for("Jolt demo finished: idle", start)
                    cmd("nav memory jolt_demo_actor protect; give force; +force_lightning; wait 15; -force_lightning; wait 8")
                    protected = status("jolt_demo_actor")
                    assert protected["health"] == 500 and protected["hits"] == 0 and protected["shock"] == 0, protected
                    print(f"PASS: {args.renderer}: protected targets do not acquire a Lightning reaction", flush=True)
                stdin.write("quit\n"); stdin.flush()
                assert process.wait(timeout=30) == 0
                return 0
            if args.collapse:
                start = len(log.read_text(errors="replace"))
                cmd("jolt_demo idle")
                wait_for("Jolt demo finished: idle", start)
                standing = status("jolt_demo_actor")
                assert standing["engaged"] and standing["phase"] == 1, standing
                cmd("set timescale .1; npc kill jolt_demo_actor", 0)
                samples = []
                for _ in range(100):
                    sample = status("jolt_demo_actor")
                    samples.append(sample)
                    if sample["sleeping"]:
                        break
                    cmd("wait 2", 0)
                (run / "collapse-results.json").write_text(json.dumps(samples, indent=2))
                assert all(s["corpse"] and s["health"] <= 0 for s in samples), samples[:3]
                assert any(0 < s["strength"] < standing["strength"] for s in samples), samples[:3]
                assert all(b["strength"] <= a["strength"] for a, b in zip(samples, samples[1:])), samples[:3]
                assert samples[-1]["strength"] == 0 and samples[-1]["sleeping"], samples[-1]
                assert abs(samples[0]["pelvis_z"]-standing["pelvis_z"]) < 4, samples[0]
                stdin.write("quit\n"); stdin.flush()
                assert process.wait(timeout=30) == 0
                print(f"PASS: {args.renderer}: standing death, gradual motor fade, passive settling", flush=True)
                return 0
            if args.floor_combat:
                for check in ("movement", "finisher"):
                    start = len(log.read_text(errors="replace"))
                    cmd("jolt_demo idle")
                    wait_for("Jolt demo finished: idle", start)
                    standing = status("jolt_demo_actor")
                    height = standing["player_origin"][2] + 26
                    weapon = 3 if check == "movement" else 1
                    cmd(f"give weaponnum {weapon}; weapon {weapon}; wait 30; jolt_select jolt_demo_actor; jolt_balance; jolt_knockdown", 0)
                    for _ in range(100):
                        fallen = status("jolt_demo_actor")
                        if fallen["grounded"]:
                            break
                        cmd("wait 1", 0)
                    else:
                        raise AssertionError("Actor did not reach the floor")
                    x, y, _ = fallen["origin"]
                    if check == "movement":
                        cmd(f"setviewpos {x-40} {y} {height} 0; +forward; wait 30; -forward", 0)
                        moved = status("jolt_demo_actor")
                        if moved["player_origin"][0] <= x+20:
                            cmd("campaign_status; screenshot_png floor_movement_failure")
                        assert moved["player_origin"][0] > x+20, (fallen, moved)
                    else:
                        cmd(f"setviewpos {x-65} {y} {height} 0; wait 2; +forward; +attack; wait 2; -forward", 0)
                        samples = []
                        for _ in range(30):
                            samples.append(status("jolt_demo_actor"))
                            if samples[-1]["health"] <= 0:
                                break
                            cmd("wait 2", 0)
                        cmd("-attack")
                        assert any(s["finisher"] for s in samples), samples[:3]
                        assert samples[-1]["health"] <= 0, samples[-1]
                        cmd("weapon 3; wait 30")
                        corpse = status("jolt_demo_actor")
                        assert corpse["sleeping"] and corpse["grounded"], corpse
                        x, y, _ = corpse["origin"]
                        cmd(f"setviewpos {x-40} {y} {height} 0; +forward; wait 30; -forward", 0)
                        assert status("jolt_demo_actor")["player_origin"][0] > x+20
                    print(f"PASS: {args.renderer}: floor {check}", flush=True)
                stdin.write("quit\n"); stdin.flush()
                assert process.wait(timeout=30) == 0
                return 0
            if args.gameplay:
                start = len(log.read_text(errors="replace"))
                cmd("jolt_demo idle")
                scene = wait_for("Jolt demo finished: idle", start)
                location = re.search(r"Jolt demo: idle at ([-\d.]+) ([-\d.]+) ([-\d.]+)", scene)
                assert location, scene
                x, y, z = map(float, location.groups())
                cmd("jolt_select jolt_demo_actor; save jolt_blast_test; set g_joltReactions 0; jolt_blast jolt_demo_actor; wait 8")
                vanilla_health = status("jolt_demo_actor")["health"]
                cmd("load jolt_blast_test; wait 40; set g_joltReactions 1; jolt_blast jolt_demo_actor; wait 2")
                blast = status("jolt_demo_actor")
                assert blast["health"] == vanilla_health and blast["hits"] == 1 and blast["engaged"] == 1, blast
                assert blast["falling"] == 1, blast
                cmd("set g_joltReactions 0; wait 8; npc kill jolt_demo_actor; wait 10")
                types = args.rig_types or ("stormtrooper", "stormtrooper2", "imperial", "reborn", "jedi",
                                          "rodian", "weequay", "trandoshan", "jan", "kyle")
                count = len(types)
                assert 2 <= count <= 16
                for i, kind in enumerate(types):
                    angle = i*360/count
                    px, py = x+32*math.cos(math.radians(angle)), y+32*math.sin(math.radians(angle))
                    cmd(f"setviewpos {px} {py} {z+40} {angle}; wait 15; npc spawn {kind} jolt_crowd_{i}; wait 8")
                    for _ in range(30):
                        if status(f"jolt_crowd_{i}")["health"] > 0:
                            break
                        cmd("wait 5")
                    else:
                        cmd(f"nav memory jolt_crowd_{i}; entitylist; nav actors; viewpos; screenshot_png crowd_spawn_failure")
                        raise AssertionError(f"NPC did not spawn alive: {kind}")
                cmd(f"set g_joltMaxBodies {count}; set g_joltReactions 1", 0)
                cmd("; ".join(f"jolt_select jolt_crowd_{i}; jolt_balance" for i in range(count)), 0)
                for i, kind in enumerate(types):
                    s = status(f"jolt_crowd_{i}")
                    assert s["engaged"] == 1 and s["active_bodies"] == count, (kind, s)
                cmd("screenshot_png crowd", 0)
                cmd("; ".join(f"jolt_select jolt_crowd_{i}; jolt_push front 2" for i in range(count)), 0)
                assert all(status(f"jolt_crowd_{i}")["engaged"] for i in range(count))
                cmd("jolt_select jolt_crowd_0; jolt_knockdown; wait 10")
                alive = status("jolt_crowd_0")
                cmd("npc kill jolt_crowd_0", 0)
                corpse = status("jolt_crowd_0")
                assert corpse["health"] <= 0 and corpse["corpse"] == 1 and corpse["engaged"] == 1, corpse
                assert abs(corpse["pelvis_z"]-alive["pelvis_z"]) < 12, (alive, corpse)
                cmd("wait 20; screenshot_png corpse; save jolt_corpse")
                saved = status("jolt_crowd_0")
                cmd("load jolt_corpse; wait 30")
                loaded = status("jolt_crowd_0")
                assert loaded["corpse"] == 1 and loaded["engaged"] == 1, loaded
                assert abs(loaded["pelvis_z"]-saved["pelvis_z"]) < 4, (saved, loaded)
                cmd("jolt_blast jolt_crowd_1; wait 4")
                assert status("jolt_crowd_1")["corpse"] == 1
                stdin.write("quit\n"); stdin.flush()
                assert process.wait(timeout=30) == 0
                print(f"PASS: {args.renderer}: {count} humanoid rigs, simultaneous active bodies, thermal blast damage, death continuity, and corpse save/load", flush=True)
                return 0
            if args.demo:
                results = {}
                cmd(f"set timescale 0.5; set g_joltDemoPush {args.push_speed}")
                cmd(f"set g_joltDebug {2 if args.debug else 0}")
                for case in args.demo_case or ("idle", "hit", "step", "leg", "run", "fall"):
                    start = len(log.read_text(errors="replace"))
                    cmd("jolt_demo " + case)
                    samples = []
                    deadline = time.monotonic() + 180
                    while f"Jolt demo finished: {case}" not in log.read_text(errors="replace")[start:]:
                        assert "Jolt demo failed:" not in log.read_text(errors="replace")[start:], log
                        if time.monotonic() > deadline:
                            raise TimeoutError(f"Demo case {case}: {log}")
                        samples.append(status())
                        cmd(f"screenshot_png demo_{case}_{len(samples):03d}", 2)
                    final = status()
                    results[case] = {"final": final, "samples": samples}
                    (run / "demo-results.json").write_text(json.dumps(results, indent=2))
                    assert final["actor"] > 0 and final["health"] > 0, final
                    if case in ("idle", "hit", "step"):
                        assert final["engaged"] == 1 and final["phase"] == 1, (case, final)
                    if case == "hit":
                        assert final["hits"] == 1 and final["peak"] > 4, final
                    if case == "step":
                        assert final.get("landings", 0) > 0, final
                    if case == "leg":
                        assert final["hits"] == 1 and final["health"] < 500, final
                        assert any(s["corrections"] > 0 or s["falling"] or s.get("peak_leg_lift", 0) > .035 for s in samples), final
                    if case == "run":
                        assert max(s["launch"] for s in samples) > 80, final
                        assert final["hits"] == 1 and final["health"] < 500, final
                    if case in ("run", "fall"):
                        assert any(s["falling"] for s in samples), (case, final)
                        assert any(s.get("brace_mask", 0) and s["falling"] for s in samples), (case, final)
                        assert any(s.get("preparing", 0) for s in samples), (case, final)
                        assert final["falling"] == 0 and final["recovering"] == 0 and final["engaged"] == 0 and abs(final["recovery_lift"]) <= 8, final
                        assert final["blend_ms"] >= 350, final
                        assert final["handoff_error"] < .1, final
                    print(f"PASS: {args.renderer}: demo {case}", flush=True)
                cmd("jolt_demo stop; set g_joltReactions 0")
                stdin.write("quit\n"); stdin.flush()
                assert process.wait(timeout=30) == 0
                return 0
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
                cmd("setviewpos 5504 -4520 64 315; wait 4; npc spawn r2d2 jolt_unsupported; wait 15")
                unsupported = status("jolt_unsupported")
                cmd("jolt_shoot jolt_unsupported; wait 4")
                untouched = status("jolt_unsupported")
                assert untouched["hits"] == 0 and untouched["health"] < unsupported["health"], untouched
                cmd("set g_joltReactions 0; wait 10")
                assert status()["active_bodies"] <= 1  # A passive corpse may still be settling.
                stdin.write("quit\n")
                stdin.flush()
                assert process.wait(timeout=30) == 0
                assert not re.search(r"Unknown command|trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY", log.read_text(errors="replace"))
                assert not re.search(r"Jolt: (active control|released control|lost support|grounded recovery)", log.read_text(errors="replace"))
                print(f"PASS: {args.renderer}: primary/alt projectiles, automatic multi-actor reactions, unchanged damage, protected/unsupported targets, disable", flush=True)
                return 0
            start = len(log.read_text(errors="replace"))
            cmd("jolt_demo idle")
            wait_for("Jolt demo finished: idle", start)
            cmd("set g_joltDebug 1; jolt_select jolt_demo_actor")
            initial = status()
            if initial["active"] != 1:
                cmd("jolt_status jolt_demo_actor; ui_report; viewpos; screenshot_png jolt_setup")
            assert initial["actor"] > 0 and initial["active"] == 1, initial
            if args.control:
                cmd("set g_joltDebug 1; jolt_balance; wait 1")
                status()
                cmd("wait 15; screenshot_png balance_neutral")
                neutral = status()
                assert neutral["engaged"] == 1 and neutral["phase"] == 1, neutral
                cmd("jolt_hit front; wait 2; screenshot_png balance_hit; wait 30")
                hit = status()
                assert hit["hits"] == 1 and hit["health"] == initial["health"] - 5, hit
                assert hit["falling"] == 0 and hit["peak"] > 4, hit
                cmd("jolt_select none; wait 30")
                returned = status("jolt_demo_actor")
                assert returned["engaged"] == 0 and returned["active"] == 1, returned
                assert returned["health"] == hit["health"], returned
                cmd("jolt_select jolt_demo_actor")
                cmd("jolt_knockdown; wait 4; screenshot_png balance_fall; wait 100")
                cmd("set g_joltReactions 0; wait 4")
                assert status()["tracked"] == 0
                stdin.write("quit\n"); stdin.flush()
                assert process.wait(timeout=30) == 0
                print(f"PASS: {args.renderer}: standing, recoil, return to animation, fall, and reset", flush=True)
                return 0
            cmd("set g_joltReactionPose 0")
            baseline = status()
            cmd("jolt_impulse left; wait 1; screenshot_png jolt_animation_only; wait 50")
            assert status()["poses"] == baseline["poses"]
            assert status()["health"] == baseline["health"]
            cmd("set g_joltReactionPose 1; jolt_impulse left; wait 1; screenshot_png jolt_physical_reaction; wait 50")
            assert status()["poses"] > baseline["poses"]
            cmd("jolt_select jolt_demo_actor; wait 4")
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
                    cmd(f"jolt_knockdown; -{direction}; wait 2; screenshot_png jolt_running_fall")
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
            cmd("set g_joltReactions 1; jolt_select jolt_demo_actor; jolt_hit front; load jolt_neutral; wait 60")
            assert status()["engaged"] == 0 and status()["hits"] == 0  # A new shadow rig is allowed after load.
            cmd("set g_joltReactions 1; jolt_select jolt_demo_actor; vid_restart; wait 60")
            assert status()["engaged"] == 0 and status()["hits"] == 0
            cmd("jolt_select jolt_demo_actor; jolt_knockdown; wait 2; save jolt_midfall; wait 4")
            assert status()["engaged"] == 0  # Save contains a normal hull and no physics overrides.
            cmd("load jolt_midfall; wait 60")
            assert status()["engaged"] == 0
            cmd("jolt_select jolt_demo_actor; npc kill jolt_demo_actor; wait 30")
            assert status("jolt_demo_actor")["engaged"] == 0
            cmd("npc spawn r2d2 jolt_other_actor; wait 30; jolt_select jolt_other_actor")
            assert status("jolt_other_actor")["active"] == 0
            stdin.write("quit\n")
            stdin.flush()
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            assert not re.search(r"Unknown command|trying to load fallback|GL_INVALID_|GL_OUT_OF_MEMORY", text), log
            print(f"PASS: {args.renderer}: localized hits, recovery, damage, slow time, disable, load, restart, death, shutdown", flush=True)
        finally:
            if recorder is not None:
                recorder.send_signal(signal.SIGINT)
                try:
                    recorder.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    recorder.kill(); recorder.wait()
            if process.poll() is None:
                process.kill()
                process.wait()
            stdin.close()
            if recorder is not None:
                assert (run / "motion.mp4").is_file() and (run / "motion.mp4").stat().st_size > 1024, "Video recording failed; check console.log"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
