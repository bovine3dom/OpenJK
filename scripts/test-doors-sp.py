#!/usr/bin/env python3
"""Check real NPC movement through automatic doors and a locked-door control."""

import argparse
import math
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    cases = {
        "hangar-in": ((6336, 638, -90), (5800, 638, -111.875), "*167", True),
        "hangar-out": ((5800, 638, -90), (6336, 638, -111.875), "*167", True),
        "ordinary": ((2772, 468, -90), (2880, 832, -103.875), "*147", True),
        "locked": ((4352, 512, -90), (3968, 512, -111.875), "*157", False),
    }
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--case", choices=cases)
    parser.add_argument("--audio", action="store_true")
    args = parser.parse_args()
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="doors.", dir=output))
    for case in ([args.case] if args.case else cases):
        start, goal, model, traversable = cases[case]
        env = dict(os.environ, OJK_SMOKE_ROOT=str(suite / case))
        if args.audio:
            env["OJK_SMOKE_SOUND"] = "1"
        # Leave room for the launcher's commands within the engine's 32-line limit.
        command = ["bash", str(root / "scripts/smoke-sp.sh"), str(args.package.resolve()),
                   "t2_wedge", "+set", "com_maxfps", "10", "+exec", "krildor-traverse.cfg",
                   "+nav", "doors", "+nav", "test", "8000", *map(str, start), *map(str, goal),
                   "+wait", "40", "+nav", "doors"]
        subprocess.run(command, env=env, check=True)
        logs = list((suite / case).glob("t2_wedge.*/console.log"))
        if len(logs) != 1:
            raise RuntimeError(f"Missing log: {suite / case}")
        records, doors = [], []
        text = logs[0].read_text(errors="replace")
        if args.audio and "steam_audio active=1" not in text:
            raise RuntimeError(f"Steam Audio did not run: {logs[0]}")
        for line in text.splitlines():
            if "routetest event=" in line:
                records.append(dict(word.split("=", 1) for word in line.split("routetest ", 1)[1].split()))
            if "navdoor " in line:
                door = dict(word.split("=", 1) for word in line.split("navdoor ", 1)[1].split())
                if door["model"] == model:
                    doors.append(door)
        if not doors or doors[0]["closed"] != "1" or not any(r["event"] == "start" for r in records):
            raise RuntimeError(f"Invalid initial door or spawn state: {logs[0]}")
        arrivals = [r for r in records if r["event"] == "arrive"]
        if traversable:
            if len(arrivals) != 1 or not any(d["closed"] == "0" for d in doors):
                raise RuntimeError(f"Door did not open and permit crossing: {logs[0]}")
            position = tuple(map(float, arrivals[0]["pos"].split(",")))
            if math.dist(position, goal) >= 24 or not any(r["event"] == "complete" for r in records):
                raise RuntimeError(f"Wrong arrival: {logs[0]}")
        elif arrivals or any(d["closed"] != "1" for d in doors) or not any(r.get("reason") == "timeout" for r in records):
            raise RuntimeError(f"Locked-door control failed: {logs[0]}")
        if not records or records[-1] != {"event": "cleanup", "remaining": "0"}:
            raise RuntimeError(f"Probe not cleaned up: {logs[0]}")
        print(f"PASS: {case}", flush=True)
    print(f"Door results: {suite}")


if __name__ == "__main__":
    main()
