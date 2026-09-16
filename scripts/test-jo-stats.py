#!/usr/bin/env python3
"""Check JO loading statistics and counter lifetime with retail transitions."""

import argparse
from contextlib import ExitStack
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("jo", Path(__file__).with_name("import-jo.py"))
assert spec and spec.loader
jo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(jo)


def fixture(profile, assets):
    folder = profile / "campaigns/jo/OpenJK"
    folder.mkdir(parents=True)
    values = (b"SET_MISSION_STATUS_SCREEN\0", b"true\0")
    script = b"IBI\0" + struct.pack("<fiiB", 1.57, 26, 2, 0)
    script += b"".join(struct.pack("<ii", 4, len(v)) + v for v in values)
    with ExitStack() as stack, zipfile.ZipFile(folder / "zzz_stats_test.pk3", "w") as dest:
        index = jo.index_assets(assets, stack)
        for name in ("kejim_post", "kejim_base"):
            data = jo.read(index, f"maps/{name}.bsp")
            offset, size = struct.unpack_from("<ii", data, 8)
            text = data[offset:offset + size].rstrip(b"\0").decode("cp1252")
            # Exercise an explicit carry boundary without changing retail assets.
            if name == "kejim_base":
                start, end = text.index("{"), text.index("}")
                world = re.sub(r'"clearstats"\s+"[^"]*"', "", text[start:end])
                text = text[:start] + world + '\n"clearstats" "0"\n' + text[end:]
                text += '\n{\n"classname" "weapon_saber"\n"origin" "320 792 24"\n}\n'
            text += '\n{\n"classname" "target_scriptrunner"\n"targetname" "stats_request"\n"usescript" "tests/stats_request"\n"count" "-1"\n}\n'
            text += '{\n"classname" "target_secret"\n"targetname" "stats_secret"\n"count" "3"\n}\n'
            dest.writestr(f"maps/{name}.ent", text.encode("cp1252"))
        dest.writestr("scripts/tests/stats_request.ibi", script)


def parse(text, kind):
    match = re.search(r"missionstats " + kind + r" ([^\n]+)", text)
    assert match, text
    return {key: quoted if quoted else plain for key, quoted, plain in re.findall(r'(\w+)=(?:"([^"]*)"|(\S+))', match[1])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-vanilla")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 640x480x24", sys.executable, __file__, *sys.argv[1:], "--inside"])
    output = ROOT / "build/jo-stats"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=args.renderer + ".", dir=output))
    profile, log = run / "profile", run / "console.log"
    assets = Path(os.environ.get("OJK_JO_ASSETS", ROOT / "GameData_JO"))
    fixture(profile, assets)
    command = ["bash", str(args.package.resolve() / "launch-sp.sh"), str(ROOT / "GameData"), "--campaign", "jo", "--new-game",
               "+safe", "+set", "cl_renderer", args.renderer, "+set", "r_fullscreen", "0", "+set", "r_mode", "3",
               "+set", "developer", "1", "+set", "logfile", "2", "+set", "d_missionStats", "1",
               "+set", "cg_thirdPerson", "0",
               "+set", "com_maxfps", "30", "+wait", "150", "+echo", "STATS_READY"]
    env = dict(os.environ, OJK_PROFILE=str(profile), OJK_JO_ASSETS=str(assets), LIBGL_ALWAYS_SOFTWARE="1",
               LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    print(f"JO statistics results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT, text=True, env=env)
        assert process.stdin
        stdin = process.stdin
        serial = 0

        def wait(pattern, start=0):
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                text = re.sub(r"\^[0-9]", "", log.read_text(errors="replace")[start:])
                if re.search(pattern, text): return text
                if process.poll() is not None: raise RuntimeError(f"Game exited: {log}")
                time.sleep(0.01)
            raise TimeoutError(f"Missing {pattern}: {log}")

        def send(text):
            assert len(text.encode()) < 256, "Console fixture command exceeds the input buffer"
            stdin.write(text + "\n")
            stdin.flush()

        def cmd(text):
            nonlocal serial
            serial += 1
            start = len(log.read_text(errors="replace"))
            marker = f"STATS_COMMAND_{serial}_DONE"
            send(f"{text}; wait 5; echo {marker}")
            return wait(marker, start)

        def ready(mapname):
            for _ in range(80):
                text = cmd("wait 10; campaign_status; missionstats_status")
                alive = re.search(r"campaign=jo map=" + mapname + r" camera=\d+ health=(\d+)", text)
                if alive and int(alive[1]) > 0 and f"missionstats live map={mapname} " in text: return text
            raise RuntimeError(f"No player on {mapname}: {log}")

        def transition(action, destination, visible, name):
            start = len(log.read_text(errors="replace"))
            send(action)
            if visible:
                wait("JO statistics: waiting for Continue", start)
                paused = parse(cmd("missionstats_status"), "live")
                time.sleep(2)
                if name == "early_stats":
                    cmd("vid_restart; wait 30")
                assert parse(cmd("missionstats_status"), "live")["time"] == paused["time"], "Next mission ran behind the statistics screen"
                image = run / (name + ".png")
                subprocess.run(["ffmpeg", "-v", "error", "-f", "x11grab", "-video_size", "640x480", "-draw_mouse", "0",
                                "-i", os.environ["DISPLAY"], "-frames:v", "1", str(image)], check=True, timeout=30)
                pixels = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(image), "-vf", "crop=540:130:50:55",
                                                  "-frames:v", "1", "-pix_fmt", "gray", "-f", "rawvideo", "-"])
                assert sum(p > 150 for p in pixels) > 150, ("Statistics text is absent", image)
                window = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--name", "."], text=True).splitlines()[-1]
                action = ["mousemove", "--window", window, "320", "420", "click", "1"] if name == "zero_stats" else ["key", "Return"]
                subprocess.run(["xdotool", "windowfocus", window, *action], check=True, timeout=10)
            else:
                wait(r"CM_LoadMap\( maps/" + destination + r"\.bsp", start)
            text = ready(destination)
            trace = log.read_text(errors="replace")[start:]
            assert ("jo_stats draw " in trace) == visible, (destination, log)
            assert parse(text, "snapshot")["visible"] == str(int(visible)), text
            return text

        def compare(live, snapshot):
            for key in ("kills", "shots", "hits", "push", "jump", "thrown", "blocks"):
                assert live[key] == snapshot[key], (key, live, snapshot)
            assert snapshot["source"] == live["map"], (live, snapshot)
            assert snapshot["secrets"] == f"{live['secrets']} of {live['total']}", snapshot
            accuracy = 100.0 * int(live["hits"]) / int(live["shots"]) if int(live["shots"]) else 0.0
            assert snapshot["accuracy"] == f"{accuracy:.2f}%", snapshot

        try:
            wait("STATS_READY")
            assert "jo_stats draw " not in log.read_text(errors="replace")
            cmd("helpusobi 1; exitview; wait 200; god; notarget; set d_npcfreeze 1; wait 150; save stats_start")
            assert (profile / "campaigns/jo/OpenJK/saves/stats_start.sav").is_file()
            cmd("use stats_secret; noclip; setviewpos 400 -2100 0 0; wait 10; npc spawn stormtrooper stats_victim; wait 30")
            actor = cmd("cinematic_status stats_victim")
            position = re.search(r"origin=([-\d.]+),([-\d.]+),([-\d.]+)", actor)
            assert position, actor
            x, y, z = map(float, position.groups())
            cmd(f"noclip; setviewpos {x - 96} {y} {z + 48} 5; wait 30; +movedown; wait 10; screenshot_png stats_aim")
            cmd("+attack; wait 90; -attack; -movedown; wait 120")
            before = parse(cmd("missionstats_status"), "live")
            assert int(before["shots"]) > 0 and int(before["hits"]) > 0 and int(before["kills"]) > 0 and before["secrets"] == "1", before
            early = before
            result = transition("use to_kejim_base", "kejim_base", True, "early_stats")
            compare(before, parse(result, "snapshot"))
            assert parse(result, "snapshot")["saber"] == "0", result
            assert parse(result, "snapshot")["favorite"] == "18", result
            assert parse(result, "live")["shots"] == before["shots"], "clearstats=0 lost carried counters"
            restarted = cmd("vid_restart; wait 60; missionstats_status")
            assert "JO statistics: waiting for Continue" not in restarted, "Renderer restart reopened acknowledged statistics"
            assert int(parse(restarted, "live")["time"]) > int(parse(result, "live")["time"]), "Renderer restart left gameplay paused"
            cmd("exitview; wait 100; helpusobi 1; set d_npcfreeze 1; setviewpos 320 792 64 180; wait 40; setForceAll 3; weapon 1; setviewpos 416 792 80 180; wait 30")
            cmd("+attack; wait 10; -attack; wait 50; +altattack; wait 40; -altattack; wait 100; force_throw; wait 50")
            state = cmd("campaign_status; missionstats_status")
            before = parse(state, "live")
            assert int(before["push"]) > 0 and int(before["thrown"]) > 0, state
            cmd("use stats_request; wait 10")
            compare(before, parse(cmd("missionstats_status"), "snapshot"))
            result = transition("maptransition artus_mine", "artus_mine", True, "force_stats")
            compare(before, parse(result, "snapshot"))
            assert parse(result, "snapshot")["saber"] == "1", result
            assert parse(result, "live")["shots"] == "0", "New mission did not reset counters"
            transition("load stats_start", "kejim_post", False, "loaded")
            zero = parse(cmd("missionstats_status"), "live")
            assert zero["shots"] == "0", zero
            # Favorite weapon measures use time, not shots. A fresh unarmed training
            # map is the no-favorite case; merely refraining from firing is not.
            transition("map yavin_trial", "yavin_trial", False, "unarmed")
            zero = parse(cmd("wait 50; missionstats_status"), "live")
            assert zero["shots"] == "0" and zero["secrets"] == "0" and zero["total"] == "1", zero
            result = transition("helpusobi 1; use end_level", "ns_streets", True, "zero_stats")
            snapshot = parse(result, "snapshot")
            compare(zero, snapshot)
            assert snapshot["accuracy"] == "0.00%" and snapshot["favorite"] == "0" and not snapshot["label"], snapshot
            transition("map valley", "valley", False, "direct")
            transition("helpusobi 1; use end_level", "yavin_temple", False, "hidden")
            send("quit")
            assert process.wait(timeout=30) == 0
            text = log.read_text(errors="replace")
            assert not re.search(r"ERROR:|Error:|Unknown command|trying to load fallback renderer", text), log
            (run / "result.json").write_text(json.dumps({"renderer": args.renderer, "early": early, "force": before, "zero": snapshot, "passed": True}, indent=2) + "\n")
            print(f"PASS: JO statistics, script request, carry/reset, zero shots, save loading, and HIDEINFO ({args.renderer})")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            (profile / "campaigns/jo/OpenJK/zz_jo_campaign.pk3").unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
