#!/usr/bin/env python3
"""Check JO mission boundaries, loadouts, optional Force points, and save/load."""

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
# The retail Cairn Bay map contains this unresolved reference tag.
RETAIL_WARNING = "ERROR: ref_tag (cinematic26) has invalid target (cinematic27)"
spec = importlib.util.spec_from_file_location("jo", ROOT / "scripts/import-jo.py")
assert spec and spec.loader
jo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(jo)


def fixture(profile, assets):
    folder = profile / "campaigns/jo/OpenJK"
    folder.mkdir(parents=True)
    exits = {
        "kejim_post": "kejim_base", "kejim_base": "artus_mine",
        "ns_streets": "ns_hideout", "ns_hideout": "bespin_undercity",
        "bespin_undercity": "cairn_bay", "cairn_bay": "doom_comm",
        "doom_comm": "yavin_swamp", "yavin_swamp": "yavin_canyon",
    }
    patches = json.loads(jo.PATCH_FILE.read_text())
    with ExitStack() as stack, zipfile.ZipFile(folder / "zz_test_preparation.pk3", "w") as output:
        assets = jo.index_assets(assets, stack)
        for source, target in exits.items():
            data = jo.read(assets, f"maps/{source}.bsp")
            start, size = struct.unpack_from("<ii", data, 8)
            entities = data[start:start + size].rstrip(b"\0")
            if source in patches:
                entities = jo.patch_entities(entities, patches[source])
            # Add only a test exit. Keep retail map geometry, NPCs, and startup scripts.
            entities += ('\n{\n"classname" "target_level_change"\n"targetname" "prep_test_exit"\n'
                         f'"mapname" "{target}"\n}}\n').encode()
            output.writestr(f"maps/{source}.ent", entities)


def fields(text, prefix):
    match = re.search(re.escape(prefix) + r" ([^\n]+)", text)
    assert match, text
    return dict(word.split("=", 1) for word in match[1].split())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/ready")
    parser.add_argument("--renderer", choices=("rdsp-vanilla", "rdsp-rend2"), default="rdsp-vanilla")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.inside:
        return subprocess.call(["xvfb-run", "-a", "-s", "-screen 0 640x480x24", sys.executable, __file__, *sys.argv[1:], "--inside"])
    output = ROOT / "build/jo-preparation"
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=args.renderer + ".", dir=output))
    profile, log = run / "profile", run / "console.log"
    assets = Path(os.environ.get("OJK_JO_ASSETS", ROOT / "GameData_JO"))
    fixture(profile, assets)
    env = dict(os.environ, OJK_PROFILE=str(profile), OJK_JO_ASSETS=str(assets), LIBGL_ALWAYS_SOFTWARE="1",
               LP_NUM_THREADS="1", SDL_AUDIODRIVER="dummy")
    command = ["bash", str(args.package.resolve() / "launch-sp.sh"), str(ROOT / "GameData"), "--campaign", "jo",
               "+safe", "+set", "cl_renderer", args.renderer, "+set", "r_fullscreen", "0", "+set", "r_mode", "3",
               "+set", "developer", "1", "+set", "logfile", "2", "+set", "com_maxfps", "30",
               "+map", "kejim_post", "+wait", "150", "+echo", "PREPARATION_READY"]
    print(f"JO preparation results: {run}", flush=True)
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=stream, stderr=subprocess.STDOUT, text=True, env=env)
        assert process.stdin
        stdin = process.stdin
        serial = 0

        def wait(pattern, start=0):
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                text = re.sub(r"\^[0-9]", "", log.read_text(errors="replace")[start:])
                if "ERROR:" in text.replace(RETAIL_WARNING, "") or process.poll() is not None:
                    raise RuntimeError(f"Game error: {log}")
                if re.search(pattern, text): return text
                time.sleep(0.02)
            raise TimeoutError(f"Missing {pattern}: {log}")

        def send(text):
            assert len(text.encode()) < 256, text
            stdin.write(text + "\n")
            stdin.flush()

        def cmd(text):
            nonlocal serial
            serial += 1
            marker = f"PREP_COMMAND_{serial}_DONE"
            start = len(log.read_text(errors="replace"))
            send(f"{text}; wait 5; echo {marker}")
            return wait(marker, start)

        def input_event(*event):
            window = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--name", "."], text=True).splitlines()[-1]
            subprocess.run(["xdotool", "windowfocus", window, *event], check=True, timeout=10)

        def click(x, y):
            if not rml:
                input_event("mousemove", str(x), str(y), "click", "1")
                cmd("wait 15")
                return
            for _ in range(20):
                ui = fields(cmd("rml_selection_status"), "rml_selection")
                left, top, _, _ = map(float, ui["viewport"].split(","))
                cx, cy = map(float, ui["cursor"].split(","))
                dx, dy = round(left + x * float(ui["scale"]) - cx), round(top + y * float(ui["scale"]) - cy)
                if abs(dx) <= 1 and abs(dy) <= 1: break
                input_event("mousemove_relative", "--", str(max(-100, min(100, dx))), str(max(-100, min(100, dy))))
                cmd("wait 2")
            else:
                raise AssertionError(f"Menu pointer did not reach {x},{y}: {log}")
            input_event("click", "1")
            cmd("wait 15")

        def capture(name):
            cmd(f"screenshot_png {name}; wait 20")
            image = profile / "campaigns/jo/OpenJK/screenshots" / (name + ".png")
            subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(image), "-frames:v", "1", "-f", "null", "-"], check=True)

        def status():
            text = cmd("jo_prepare status; campaign_status")
            powers = {name: (int(original), int(selected), int(live)) for name, original, selected, live in
                      re.findall(r"jo_prepare power=(\w+) original=(\d+) selected=(\d+) live=(\d+)", text)}
            return fields(text, "jo_prepare"), powers, text

        def ready(mapname):
            for _ in range(80):
                text = cmd("wait 15; campaign_status")
                player = re.search(r"campaign=jo map=" + mapname + r" camera=\d+ health=(\d+)", text)
                if player and int(player[1]) > 0: return text
            raise AssertionError(f"Map did not start: {mapname}")

        def leave(destination, budget=None, exit_name="prep_test_exit"):
            start = len(log.read_text(errors="replace"))
            cmd(f"helpusobi 1; exitview; wait 30; use {exit_name}")
            wait("JO statistics: waiting for Continue", start)
            input_event("key", "Return")
            if budget is not None:
                wait("JO preparation: ready", start)
                data, powers, _ = status()
                assert data["target"] == destination and int(data["budget"]) == budget, data
                return data, powers
            ready(destination)
            data, _, _ = status()
            assert data["pending"] == "0", data
            assert "JO preparation: ready" not in log.read_text(errors="replace")[start:]

        def commit(destination):
            cmd("jo_prepare commit")
            ready(destination)
            cmd("helpusobi 1; exitview; wait 100; god; notarget")

        try:
            wait("PREPARATION_READY")
            rml = "RmlUi: JA selection menus ready" in log.read_text(errors="replace")
            cmd("helpusobi 1; exitview; wait 150; god; notarget")
            assert "rejected" in cmd("jo_prepare begin")
            leave("kejim_base")
            leave("artus_mine", 0)
            cmd("jo_prepare force absorb 1")
            assert status()[1]["absorb"][1] == 0, "Force spending was available before training"
            capture("before_force_training")
            commit("artus_mine")

            cmd("map yavin_trial; wait 150; helpusobi 1; give weaponnum 1; setSaberOffense 1")
            cmd("setForceJump 1; setForcePush 1; setForcePull 1; setForceSpeed 1; wait 50")
            leave("ns_streets", 2, "end_level")
            before = re.search(r"forcelevels [^\n]+", status()[2])
            assert before
            cmd("jo_prepare weapon 3; jo_prepare weapon 4; jo_prepare weapon 13; jo_prepare weapon 9; jo_prepare explosive 12")
            cmd("jo_prepare weapon 8; jo_prepare explosive 1; jo_prepare force heal 1")
            data, _, text = status()
            assert int(data["primary"]) == (1 << 13) | (1 << 9) and data["explosive"] == "12", data
            after = re.search(r"forcelevels [^\n]+", text)
            assert after and after[0] == before[0], "A story-controlled power changed"
            click(*((240, 228) if rml else (590, 278)))
            assert status()[1]["sense"][1] == 1, "Mouse upgrade did not work"
            cmd("jo_prepare force sense 1; jo_prepare force rage 1")
            data, powers, _ = status()
            assert data["remaining"] == "0" and powers["sense"] == (0, 2, 0) and powers["rage"][1] == 0
            cmd("use end_level")
            assert status()[1]["sense"] == (0, 2, 0), "A repeated exit reset the draft"
            clock = fields(cmd("missionstats_status"), "missionstats live")["time"]
            cmd("toggleconsole; wait 15")
            start = len(log.read_text(errors="replace"))
            input_event("type", "--clearmodifiers", "echo PREP_CONSOLE_INPUT")
            input_event("key", "Return")
            wait("PREP_CONSOLE_INPUT", start)
            cmd("set cl_paused 0; wait 15; toggleconsole")
            assert fields(cmd("missionstats_status"), "missionstats live")["time"] == clock, "Preparation let the finished mission advance"
            cmd("jo_prepare force sense -1; jo_prepare force sense 1; vid_restart; wait 60")
            assert status()[1]["sense"] == (0, 2, 0), "Renderer restart lost the draft or applied it early"
            cmd("save pending_preparation")
            assert not (profile / "campaigns/jo/OpenJK/saves/pending_preparation.sav").exists()
            capture("optional_force_and_weapons")
            if rml:
                click(405, 459)
                capture("weapons")
                click(240, 459)
                assert fields(cmd("rml_selection_status"), "rml_selection")["page"] == "1"
                assert status()[1]["sense"] == (0, 2, 0), "Switching pages changed the draft"
                click(405, 459)
                click(510, 459)
            else:
                input_event("key", "Up", "Return")
            ready("ns_streets")
            cmd("helpusobi 1; exitview; wait 100")
            data, powers, text = status()
            assert data["pending"] == "0" and powers["sense"][2] == 2, text
            mask = int(fields(text, "jo_loadout")["weapons"])
            expected = (1 << 13) | (1 << 9) | (1 << 12) | (1 << 18) | (1 << 1) | 1
            assert mask == expected, (mask, expected, text)
            cmd("load auto_ns_streets; wait 100; exitview; wait 100")
            _, restored, text = status()
            assert restored["sense"][2] == 2 and int(fields(text, "jo_loadout")["weapons"]) == expected, "Entry autosave lost preparation choices"
            cmd("save prepared_ns; load prepared_ns; wait 100")
            assert status()[1]["sense"][2] == 2
            leave("ns_hideout")
            assert status()[1]["sense"][2] == 2, "Internal map transition lost Force purchases"
            cmd("helpusobi 1; exitview; wait 50; weapon 13; wait 60")
            ammo = int(fields(cmd("jo_prepare status"), "jo_loadout")["ammo"])
            cmd("+attack; wait 10; -attack; wait 30")
            assert int(fields(cmd("jo_prepare status"), "jo_loadout")["ammo"]) < ammo, "JA concussion rifle did not fire"
            sight = cmd("force_sight; wait 15; jo_prepare status")
            assert int(fields(sight, "jo_loadout")["force_active"]) & (1 << 15), "Purchased Sense did not activate"
            cmd("force_sight; wait 15")

            for destination, budget, upgrades in (("bespin_undercity", 3, ("sense",)),
                                                  ("cairn_bay", 4, ("rage",)),
                                                  ("doom_comm", 5, ("protect",)),
                                                  ("yavin_swamp", 7, ("absorb", "drain"))):
                leave(destination, budget)
                if budget > 3:
                    cmd("jo_prepare force sense 1; jo_prepare force sense -1")
                    assert status()[1]["sense"][1] == 3, "Rank cap or previous purchases changed"
                for power in upgrades: cmd(f"jo_prepare force {power} 1")
                assert status()[0]["remaining"] == "0"
                commit(destination)
                native = re.search(r"forcelevels [^\n]+", status()[2])
                expected_jump = 2 if budget < 5 else 3
                assert native and f"jump={expected_jump}" in native[0], "Story-controlled Jump did not advance"
            assert sum(values[2] for values in status()[1].values()) == 7, "Final budget is not seven of fifteen ranks"
            leave("yavin_canyon")
            assert sum(values[2] for values in status()[1].values()) == 7
            cmd("load prepared_ns; wait 100")
            assert status()[1]["sense"][2] == 2 and sum(v[2] for v in status()[1].values()) == 2
            leave("ns_hideout")
            prepared = leave("bespin_undercity", 3)
            assert prepared
            data, _ = prepared
            assert data["remaining"] == "1", "Reloading an earlier save awarded extra points"
            cmd("load prepared_ns; wait 100")
            ready("ns_streets")
            data, powers, _ = status()
            assert data["pending"] == "0" and powers["sense"][2] == 2, "Loading a save did not cancel preparation"
            send("quit")
            assert process.wait(timeout=30) == 0
            assert not re.search(r"ERROR:|Error:|Unknown command|unknown menu keyword", log.read_text(errors="replace").replace(RETAIL_WARNING, "")), log
            print(f"PASS: JO true-mission loadouts, story powers, seven optional ranks, UI, and saves ({args.renderer})")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            (profile / "campaigns/jo/OpenJK/zz_jo_campaign.pk3").unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
