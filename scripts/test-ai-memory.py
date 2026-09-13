#!/usr/bin/env python3
"""Check real stormtrooper sight memory and lost-contact goals headlessly."""

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


def point(sample, key):
    return tuple(map(float, sample[key].split(",")))


def main():
    root = Path(__file__).resolve().parent.parent
    cases = {"contact-async": ("ai-memory-test.cfg", 1),
             "contact-sync": ("ai-memory-test.cfg", 0),
             "unseen": ("ai-memory-hidden.cfg", 1),
             "search": ("ai-memory-search.cfg", 1),
             "shared-async": ("ai-memory-shared.cfg", 1),
             "shared-sync": ("ai-memory-shared.cfg", 0),
             "switch": ("ai-memory-switch.cfg", 1)}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--case", choices=cases)
    args = parser.parse_args()
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="memory.", dir=output))
    for case in ([args.case] if args.case else cases):
        config, asynchronous = cases[case]
        env = dict(os.environ, OJK_SMOKE_ROOT=str(suite / case))
        subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(args.package.resolve()),
                        "t2_wedge", "+set", "com_maxfps", "10", "+set", "d_asynchronousGroupAI",
                        str(asynchronous), "+exec", config], env=env, check=True)
        logs = list((suite / case).glob("t2_wedge.*/console.log"))
        check(len(logs) == 1, f"Missing unique log: {suite / case}")
        text = logs[0].read_text(errors="replace")
        all_samples = {}
        phase = ""
        for line in text.splitlines():
            label = re.search(r"OJK_MEMORY_([A-Z_]+)$", line)
            if label:
                phase = label[1]
            if "aimemory event=sample " in line:
                sample = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
                all_samples.setdefault(phase, {})[sample["name"]] = sample
        samples = {phase: actors["_memory_a"] for phase, actors in all_samples.items()}
        check("aimemory event=rejected" not in text, f"Fixture control rejected: {logs[0]}")
        if case.startswith("shared-") or case == "switch":
            initial = all_samples["SHARED_INITIAL"]
            hidden = all_samples["SHARED_HIDDEN"]
            updated = all_samples["SHARED_MEMBER"]
            for name in ("_memory_a", "_memory_b"):
                check(initial[name]["los"] == "1" and hidden[name]["los"] == "0", str(hidden))
                check(initial[name]["members"] == "2" and updated[name]["members"] == "2", str(updated))
                check(int(hidden[name]["time"]) - int(hidden[name]["seen_time"]) > 7000, str(hidden))
                for key in ("seen_time", "seen", "group_time", "shared", "group"):
                    check(hidden[name][key] == initial[name][key], f"Hidden member lost its memory: {hidden}")
            a, b = updated["_memory_a"], updated["_memory_b"]
            check(a["group"] == b["group"] != "-1", str(updated))
            check(a["los"] == "0" and b["los"] == "1", str(updated))
            check(a["seen_time"] == initial["_memory_a"]["seen_time"] and a["seen"] == initial["_memory_a"]["seen"], str(a))
            check(int(b["seen_time"]) > int(hidden["_memory_b"]["seen_time"]), str(b))
            check(a["group_time"] == b["group_time"] == b["seen_time"], str(updated))
            check(point(a, "shared") == point(b, "shared") == point(b, "seen") == point(b, "target"), str(updated))
            if case == "switch":
                switched = all_samples["SWITCHED"]
                a, b, c = (switched[name] for name in ("_memory_a", "_memory_b", "_memory_c"))
                check(a["enemy"] == a["group_enemy"] == c["ent"], str(switched))
                check(b["enemy"] == b["group_enemy"] == "0" and a["group"] != b["group"], str(switched))
                check(point(a, "seen") == point(a, "shared") == point(c, "pos"), str(switched))
        elif case == "unseen":
            unseen = samples["UNSEEN"]
            check(unseen["enemy"] == "0" and unseen["los"] == "0" and unseen["pvs"] == "1", str(unseen))
            check(unseen["seen_time"] == "0" and unseen["group_time"] == "0" and unseen["group"] == "-1",
                  f"Unseen enemy fabricated sight or a group: {unseen}")
        else:
            visible = samples["VISIBLE"]
            check(visible["los"] == "1" and visible["group_enemy"] == "0", str(visible))
            check(0 <= int(visible["time"]) - int(visible["seen_time"]) <= 500, str(visible))
            check(point(visible, "seen") == point(visible, "target") == point(visible, "shared"), str(visible))
            check(visible["seen_time"] == visible["group_time"], str(visible))
            if case == "search":
                expired = samples["EXPIRED"]
                check(int(expired["time"]) - int(expired["group_time"]) > 180000, str(expired))
                check(expired["group_time"] == visible["group_time"] and expired["los"] == "0", str(expired))
                searched = samples["SEARCH"]
                check(searched["enemy"] == "-1" and int(searched["home"]) > 0, str(searched))
                check(math.dist(point(searched, "goal_pos"), point(visible, "shared")) < 1,
                      f"Search did not start at the remembered location: {searched}")
                check("action=dissolve source=group_last_seen" in text, "No remembered-position search")
            else:
                for phase in ("HIDDEN_B", "HIDDEN_C", "TRACK"):
                    hidden = samples[phase]
                    check(hidden["los"] == "0" and hidden["pvs"] == "1", str(hidden))
                    check(int(hidden["time"]) - int(hidden["seen_time"]) > 7000, str(hidden))
                    for key in ("seen_time", "seen", "group", "group_time", "shared", "clear_time"):
                        check(hidden[key] == visible[key], f"Hidden target changed {key}: {hidden}")
                check(math.dist(point(samples["HIDDEN_B"], "target"), point(samples["HIDDEN_C"], "target")) > 100,
                      "Hidden positions did not change")
                check(point(samples["TRACK"], "goal_pos") == point(visible, "shared"), "Goal followed hidden target")
                check("action=track source=group_last_seen" in text, "No remembered-position tracking")
                reacquired = samples["REACQUIRED"]
                check(reacquired["los"] == "1" and int(reacquired["seen_time"]) > int(visible["seen_time"]), str(reacquired))
                check(point(reacquired, "seen") == point(reacquired, "target") == point(reacquired, "shared"), str(reacquired))
                check(reacquired["seen_time"] == reacquired["group_time"], str(reacquired))
        print(f"PASS: {case}", flush=True)
    print(f"Memory results: {suite}")


if __name__ == "__main__":
    main()
