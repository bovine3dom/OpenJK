#!/usr/bin/env python3
"""Verify local reports, supported flanks, regrouping, and interruption."""

import argparse
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def point(record, key="pos"):
    return tuple(map(float, record[key].split(",")))


def main():
    root = Path(__file__).resolve().parent.parent
    cases = {"recruit": ("recruit", 1), "ignore": ("ignore", 1), "nogroups": ("nogroups", 1),
             "flank-async": ("flank", 1), "flank-sync": ("flank", 0),
             "regroup": ("regroup", 1), "solo": ("solo", 1), "cancel": ("cancel", 1), "save": ("save", 1)}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--case", choices=cases)
    args = parser.parse_args()
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="tactics.", dir=output))
    for case in ([args.case] if args.case else cases):
        fixture, asynchronous = cases[case]
        subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(args.package.resolve()), "t2_wedge",
                        "+set", "com_maxfps", "10", "+set", "d_asynchronousGroupAI", str(asynchronous),
                        "+exec", f"ai-squad-{fixture}.cfg"],
                       env=dict(os.environ, OJK_SMOKE_ROOT=str(suite / case)), check=True)
        logs = list((suite / case).glob("t2_wedge.*/console.log"))
        check(len(logs) == 1, f"Missing log: {suite / case}")
        text = logs[0].read_text(errors="replace")
        check("aimemory event=rejected" not in text, f"Rejected fixture control: {logs[0]}")
        samples, events, phase = {}, [], ""
        for line in text.splitlines():
            line = re.sub(r"\^[0-9]", "", line)
            label = re.search(r"OJK_SQUAD_([A-Z_]+)$", line)
            if label:
                phase = label[1]
            if "aimemory event=sample " in line:
                sample = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
                samples.setdefault(phase, {})[sample["name"]] = sample
            if "squad event=" in line:
                event = dict(word.split("=", 1) for word in line.split("squad ", 1)[1].split() if "=" in word)
                events.append(event)
        reports = [e for e in events if e["event"] == "report_delivery"]
        if case == "recruit":
            a, b = (samples["RECRUITED"][name] for name in ("_memory_a", "_memory_b"))
            check(a["los"] == "1" and b["los"] == "0" and b["seen_time"] == "0", str(samples))
            check(a["group"] == b["group"] != "-1" and b["enemy"] == "0" and b["members"] == "2", str(b))
            check(len(reports) == 1 and reports[0]["source"] == a["ent"] and reports[0]["recipient"] == b["ent"], str(reports))
            check(any(e["event"] == "report_ack_attempt" and e["ent"] == b["ent"] for e in events), "No report acknowledgement attempt")
            check(math.dist(point(reports[0], "pos"), point(a, "shared")) < 0.1, str(reports))
            for name in ("_memory_a", "_memory_b"):
                before, after = samples["RECRUITED"][name], samples["STALE"][name]
                check(after["los"] == "0" and int(after["time"]) - int(after["group_time"]) > 1500, str(after))
                for key in ("seen", "seen_time", "group_time", "shared"):
                    check(after[key] == before[key], f"Report fabricated fresh sight: {after}")
        elif case in ("ignore", "nogroups"):
            a, b = (samples["IGNORED"][name] for name in ("_memory_a", "_memory_b"))
            check(b["group"] != a["group"] and b["seen_time"] == "0", str(b))
            check(not any(e["recipient"] == b["ent"] for e in reports), str(reports))
        elif case.startswith("flank") or case == "cancel":
            a, b = (samples["MOVING"][name] for name in ("_memory_a", "_memory_b"))
            check(a["role"] == "5" and b["role"] == "3", str(samples["MOVING"]))
            check(point(b)[1] < 450 and b["los"] == "0", f"Flanker did not use concealed south route: {b}")
            known, goal = point(b, "tactic_threat"), point(b, "tactic_goal")
            support_direction = (point(a)[0]-known[0], point(a)[1]-known[1])
            flank_direction = (goal[0]-known[0], goal[1]-known[1])
            cosine = sum(x*y for x, y in zip(support_direction, flank_direction)) / (math.hypot(*support_direction)*math.hypot(*flank_direction))
            check(cosine <= 0.5 and math.dist(goal, known) >= 128, "Invalid flank geometry")
            active = set()
            for event in events:
                if event["event"] == "tactic_assign" and event["role"] == "3":
                    check(not active, f"Concurrent flankers: {event}")
                    active.add(event["ent"])
                elif event["event"] == "tactic_finish" and event["role"] in ("3", "4"):
                    active.discard(event["ent"])
                elif event["event"] == "tactic_step" and event["role"] == "3":
                    check(math.dist(point(event), point(event, "knownposition")) >= 128, f"Flanker crossed threat radius: {event}")
            if case == "cancel":
                check(all(s["role"] == "0" for s in samples["CANCELLED"].values()), str(samples["CANCELLED"]))
                check(any(e["event"] == "tactic_finish" and e.get("reason") == "support_lost" for e in events), "No support-loss cleanup")
            else:
                arrived = samples["FINISHED"]["_memory_b"]
                check(arrived["role"] in ("0", "4") and math.dist(point(arrived), goal) < 24, str(arrived))
                check(any(e["event"] == "tactic_arrival" and e["ent"] == b["ent"] for e in events), "No physical flank arrival")
        elif case in ("regroup", "solo"):
            moving = samples["RETREAT" if case == "regroup" else "SOLO"]["_memory_a"]
            check(moving["role"] in ("1", "2"), str(moving))
            check(not any(e["event"] == "tactic_assign" and e["role"] == "3" for e in events), "Unsupported flank")
            if case == "regroup":
                final = samples["REGROUPED"]["_memory_a"]
                check(final["role"] in ("0", "2") and final["los"] == "0", str(final))
                check(math.dist(point(final), point(final, "tactic_threat")) > math.dist(point(moving), point(moving, "tactic_threat")), "Retreat did not gain distance")
                check(any(e["event"] == "tactic_arrival" for e in events), "No regroup arrival")
        elif case == "save":
            before, after = (samples[p]["_memory_a"] for p in ("SAVE", "RESTORED"))
            check(before["role"] == "1" and after["role"] in ("1", "2"), str(samples))
            for key in ("cp", "tactic_goal", "tactic_threat", "health", "enemy"):
                check(after[key] == before[key], f"Tactic did not survive save/load: {samples}")
        print(f"PASS: {case}", flush=True)
    print(f"Squad results: {suite}")


if __name__ == "__main__":
    main()
