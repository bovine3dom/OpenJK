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
             "regroup": ("regroup", 1), "solo": ("solo", 1), "cancel": ("cancel", 1), "save": ("save", 1),
             **{name: (name, 1) for name in ("death", "timeout", "cinematic", "contested", "save-reservation", "cp-low", "cp-high")}}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=root / "build/ready")
    parser.add_argument("--case", choices=cases)
    args = parser.parse_args()
    for fixture in root.joinpath("scripts").glob("ai-squad-*.cfg"):
        staged = args.package / "OpenJK" / fixture.name
        check(staged.is_file() and staged.read_bytes() == fixture.read_bytes(),
              f"Stage current {fixture.name} in {args.package / 'OpenJK'}")
    output = root / "build/smoke"
    output.mkdir(parents=True, exist_ok=True)
    suite = Path(tempfile.mkdtemp(prefix="tactics.", dir=output))
    for case in ([args.case] if args.case else cases):
        fixture, asynchronous = cases[case]
        mode = [] if asynchronous else ["+exec", "ai-squad-sync.cfg"]
        subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(args.package.resolve()), "t2_wedge",
                        "+exec", "ai-squad-settings.cfg", *mode,
                        "+exec", f"ai-squad-{fixture}.cfg"],
                       env=dict(os.environ, OJK_SMOKE_ROOT=str(suite / case)), check=True)
        logs = list((suite / case).glob("t2_wedge.*/console.log"))
        check(len(logs) == 1, f"Missing log: {suite / case}")
        text = logs[0].read_text(errors="replace")
        check("aimemory event=rejected" not in text, f"Rejected fixture control: {logs[0]}")
        check(not re.search(r"couldn't exec|Unknown command|ERROR:", text), f"Fixture error: {logs[0]}")
        samples, events, controls, phase = {}, [], [], ""
        for line in text.splitlines():
            line = re.sub(r"\^[0-9]", "", line)
            label = re.search(r"OJK_SQUAD_([A-Z_]+)$", line)
            if label:
                phase = label[1]
            if "aimemory event=sample " in line:
                sample = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
                samples.setdefault(phase, {})[sample["name"]] = sample
            elif "aimemory event=lifecycle " in line:
                state = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
                samples[phase][state["name"]].update(state)
            elif any(f"aimemory event={kind} " in line for kind in ("reservation", "reuse", "cp")):
                controls.append(dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split()))
            if "squad event=" in line:
                event = dict(word.split("=", 1) for word in line.split("squad ", 1)[1].split() if "=" in word)
                event["phase"] = phase
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
        elif case in ("cp-low", "cp-high"):
            former, owner = ("_memory_a", "_memory_b") if case == "cp-low" else ("_memory_b", "_memory_a")
            initial = samples["OWNED"]
            check((int(initial[former]["ent"]) < int(initial[owner]["ent"])) == (case == "cp-low"), "Wrong entity order")
            for phase in ("OWNED", "RELEASED", "SAVED", "RESTORED", "REUSED", "FAILED", "VACATED", "CLEANED"):
                expected_owner = former if phase == "OWNED" else None if phase in ("RELEASED", "CLEANED") else owner
                for name in (former, owner):
                    state = samples[phase][name]
                    # Inactive tacticCP keeps its spawn value; combat_cp is the reservation.
                    check(state["role"] == "0" and state["cp"] == initial[name]["cp"], f"Tactical state changed: {state}")
                    check(state["combat_cp"] == ("0" if name == expected_owner else "-1")
                          and state["occupied"] == ("1" if name == expected_owner else "0"), f"Wrong owner at {phase}: {state}")
                    check(state["ent"] == initial[name]["ent"], f"Entity changed at {phase}: {state}")
            operations = [(c["name"], c["action"], c["cp"], c["result"], c["occupied"])
                          for c in controls if c["event"] == "cp"]
            check(operations == [(former, "cp", "0", "1", "1"), (former, "release", "0", "1", "0"),
                                 (owner, "cp", "0", "1", "1"), (former, "release", "-1", "0", "0"),
                                 (former, "cp", "1", "1", "1"), (former, "cp", "0", "0", "1"),
                                 (former, "cp", "1", "1", "1"), (former, "vacate", "1", "0", "0"),
                                 (owner, "release", "0", "1", "0")], f"Wrong reservation results: {operations}")
        else:
            before = samples["PREPARED"]["_memory_b"]
            check(before["role"] == "3" and samples["PREPARED"]["_memory_a"]["role"] == "5", str(samples))
            check(int(before["cp"]) >= 0 and before["cp"] == before["combat_cp"] and before["occupied"] == "1", str(before))
            check(before["speech"] == "7" and float(before["speech_chance"]) == 0.5, f"No queued bark: {before}")
            check(any(c["event"] == "reservation" and c["cp"] == before["cp"] and c["contested"] == "0" for c in controls), str(controls))
            if case == "save-reservation":
                for name, saved in samples["PREPARED"].items():
                    restored = samples["RESTORED"][name]
                    for key in ("role", "cp", "combat_cp", "occupied", "deadline", "tactic_goal", "tactic_threat", "speech", "speech_chance"):
                        check(saved[key] == restored[key], f"Save changed {key}: {saved} -> {restored}")
            for state in samples["CLEANED"].values():
                check(state["role"] == "0" and state["cp"] == "-1", f"Role not cleared: {state}")
                check(state["speech"] == "0" and float(state["speech_chance"]) == 0, f"Stale queued bark: {state}")
            after = samples["CLEANED"]["_memory_b"]
            check(after["combat_cp"] == "-1", f"Stale reservation: {after}")
            check(any(e["event"] == "cp_release" and e["cp"] == before["cp"]
                      and e["phase"] in ("PREPARED", "RESTORED") for e in events), "No reservation release")
            check(not any(e["event"] == "movement_speech_consume" and e["ent"] == before["ent"]
                          and e["phase"] in ("PREPARED", "CLEANED", "LATER", "RESTORED") for e in events), "Cancelled bark consumed")
            if case == "death":
                check(int(after["health"]) <= 0 and after["group"] == "-1", str(after))
                check(samples["LATER"]["_memory_b"]["speech"] == "0", "Dead actor retained speech")
            elif case == "timeout":
                check(any(e["event"] == "tactic_finish" and e["ent"] == before["ent"] and e["reason"] == "timeout" for e in events), "No timeout cleanup")
                check(math.dist(point(after), point(before, "tactic_goal")) > 24, "Timeout fixture reached the goal")
            elif case == "cinematic":
                check(after["goal"] == "0" and after["behavior"] != before["behavior"], f"Script goal lost: {after}")
            if case in ("contested", "save-reservation"):
                check(any(c["event"] == "reuse" and c["cp"] == before["cp"] and c["reserved"] == c["occupied"] == "1" for c in controls), "Stale cleanup freed reused point")
        print(f"PASS: {case}", flush=True)
    print(f"Squad results: {suite}")


if __name__ == "__main__":
    main()
