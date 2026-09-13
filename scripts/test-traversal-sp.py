#!/usr/bin/env python3
"""Run isolated NPC locomotion and lifecycle checks under the headless harness."""

import argparse
import math
import os
from pathlib import Path
import subprocess
import tempfile


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    root = Path(__file__).resolve().parent.parent
    cases = ("south", "north", "direct", "blocked", "cancel", "frozen", "removed", "save")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--case", choices=cases)
    args = parser.parse_args()
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="traversal.", dir=output))
    start = (2772, 468, -90)
    finish = (2368, 556, -103.875)
    paths = {
        "south": [(2668, 404, -103.875), (2560, 320, -103.875), (2432, 384, -103.875), finish],
        "north": [(2688, 640, -103.875), (2560, 704, -103.875), (2452, 620, -103.875), finish],
    }
    cases = (args.case,) if args.case else cases
    for case in cases:
        goals = paths.get(case, [finish])
        timeout = 8000
        if case == "blocked":
            goals = [(2560, 512, -103.875)]
            timeout = 2000
        elif case == "frozen":
            timeout = 1000
        command = ["+nav", "test", str(timeout), *[str(v) for point in [start, *goals] for v in point]]
        if case == "cancel":
            # A second start must not replace the active actor. A new start after cancel must work.
            command += command.copy() + ["+wait", "2", "+nav", "test", "cancel"]
            command += ["+nav", "test", "8000", *map(str, start), *map(str, finish)]
        elif case == "frozen":
            # Bare cvar commands run here; +set also applies during engine startup.
            command += ["+d_npcfreeze", "1"]
        elif case == "removed":
            command += ["+wait", "2", "+npc", "kill", "_route_test"]
        elif case == "save":
            command += ["+wait", "2", "+save", "route_probe"]
        command += ["+wait", "250"]
        env = dict(os.environ, OJK_SMOKE_ROOT=str(suite / case))
        subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(args.package.resolve()),
                        "t2_wedge", "+exec", "krildor-traverse.cfg", *command], env=env, check=True)
        logs = list((suite / case).glob("t2_wedge.*/console.log"))
        check(len(logs) == 1, f"Unexpected log count: {suite / case}")
        text = logs[0].read_text(errors="replace")
        check("OJK_KRILDOR_TRAVERSE_READY" in text, f"Fixture did not finish: {logs[0]}")
        records = [dict(word.split("=", 1) for word in line.split("routetest ", 1)[1].split() if "=" in word)
                   for line in text.splitlines() if "routetest event=" in line]
        starts = [r for r in records if r["event"] == "start"]
        check(len(starts) == (2 if case == "cancel" else 1), f"Probe did not start: {logs[0]}")
        for record in starts:
            check(record["weapon"] == "0", f"Probe is armed: {record}")
            position = tuple(map(float, record["pos"].split(",")))
            check(math.dist(position, (2772, 468, -103.875)) < 2, f"Wrong spawn position: {record}")
        check(records[-1] == {"event": "cleanup", "remaining": "0"}, f"Missing terminal cleanup: {logs[0]}")
        failures = [r for r in records if r["event"] == "failed"]
        if case in ("blocked", "frozen", "removed"):
            reason = "dead" if case == "removed" else "timeout"
            check(len(failures) == 1 and failures[0].get("reason") == reason, f"Wrong failure: {logs[0]}")
            check(not any(r["event"] in ("arrive", "complete") for r in records), f"False arrival: {logs[0]}")
        elif case == "save":
            check(any(r["event"] == "cancelled" and r.get("reason") == "save" for r in records), f"Save did not cancel: {logs[0]}")
            check(not failures and not any(r["event"] == "complete" for r in records), f"False completion: {logs[0]}")
        else:
            check(not failures and sum(r["event"] == "complete" for r in records) == 1, f"Route failed: {logs[0]}")
            if case == "cancel":
                check(any(r["event"] == "rejected" and r.get("reason") == "busy" for r in records), f"Busy guard failed: {logs[0]}")
                check(sum(r["event"] == "cancelled" for r in records) == 1, f"Cancel failed: {logs[0]}")
            else:
                arrivals = [r for r in records if r["event"] == "arrive"]
                check([int(r["leg"]) for r in arrivals] == list(range(1, len(goals) + 1)), f"Missing route legs: {logs[0]}")
                for record, goal in zip(arrivals, goals):
                    position = tuple(map(float, record["pos"].split(",")))
                    distance = math.dist(position, goal)
                    check(distance < 24 and abs(position[2] - goal[2]) < 2, f"Wrong arrival: {record}")
                    check(abs(distance - float(record["distance"])) < 0.01, f"Wrong distance: {record}")
                visible = [r["los"] for r in records if r["event"] in ("position", "arrive")]
                check("1" in visible and visible[-1] == "0", f"Missing loss of line of sight: {logs[0]}")
                if case == "direct":
                    check(any(int(r.get("nodes", 0)) > 0 for r in records), f"Navigator was not exercised: {logs[0]}")
        print(f"PASS: {case}", flush=True)
    print(f"Traversal results: {suite}")


if __name__ == "__main__":
    main()
