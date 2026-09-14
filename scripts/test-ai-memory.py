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


def check_path(sample):
    check(sample["group_wp"] == sample["seen_wp"] != "0", f"Group waypoint is not the observed target waypoint: {sample}")
    check(sample["member_wp"] == sample["actor_wp"] != "0", f"Path source is not the member waypoint: {sample}")
    check(0 <= int(sample["path_cost"]) < 16777216, f"No member path to the observed target: {sample}")


def main():
    root = Path(__file__).resolve().parent.parent
    cases = {"contact-async": ("ai-memory-test.cfg", 1),
             "contact-sync": ("ai-memory-test.cfg", 0),
             "unseen": ("ai-memory-hidden.cfg", 1),
             "search": ("ai-memory-search.cfg", 1),
             "shared-async": ("ai-memory-shared.cfg", 1),
             "shared-sync": ("ai-memory-shared.cfg", 0),
             "switch": ("ai-memory-switch.cfg", 1),
             "short-async": ("ai-memory-short.cfg", 1),
             "short-sync": ("ai-memory-short.cfg", 0),
             "solo": ("ai-memory-solo.cfg", 1),
             "solo-unseen": ("ai-memory-solo-unseen.cfg", 1),
             "solo-switch": ("ai-memory-solo-switch.cfg", 1),
             "door": ("ai-memory-door.cfg", 1)}
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
                        str(asynchronous), "+set", "d_squadTactics", "0", "+exec", config], env=env, check=True)
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
            elif "aimemory event=path " in line:
                path = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
                all_samples[phase][path["name"]].update(path)
        samples = {phase: actors["_memory_a"] for phase, actors in all_samples.items()}
        check("aimemory event=rejected" not in text, f"Fixture control rejected: {logs[0]}")
        if case.startswith("solo"):
            check(all(sample["troop"] == "0" for sample in samples.values()), "Solo fixture entered legacy troop AI")
        if case == "door":
            visible, hidden, followed = (samples[phase] for phase in ("DOOR_VISIBLE", "DOOR_HIDDEN", "DOOR_FOLLOW"))
            # The door may start closing before the first snapshot is printed.
            check(0 <= int(visible["time"]) - int(visible["group_time"]) < 2000, str(visible))
            check(point(visible, "seen") == point(visible, "shared") == point(visible, "target"), str(visible))
            check(hidden["los"] == "0", str(hidden))
            check(int(hidden["time"]) - int(hidden["group_time"]) > 7000, str(hidden))
            check(point(hidden, "shared") == point(visible, "target"), str(hidden))
            check(hidden["group"] == visible["group"] != "-1", str(hidden))
            # Ranged AI can stop when opening the door restores a clear shot.
            check(point(followed, "pos")[0] < 6188 and followed["los"] == "1", str(followed))
            check(int(followed["group_time"]) > int(hidden["time"]), str(followed))
            check("action=track source=group_last_seen" in text, "No remembered-position pursuit")
            hidden_log = text.split("OJK_MEMORY_DOOR_HIDDEN", 1)[1].split("OJK_MEMORY_DOOR_FOLLOW", 1)[0]
            check(re.search(r"navdoor .*name=Hanger_door1 .*closed=1", hidden_log), "Door was not closed before pursuit")
        elif case.startswith("shared-") or case == "switch":
            initial = all_samples["SHARED_INITIAL"]
            hidden = all_samples["SHARED_HIDDEN"]
            updated = all_samples["SHARED_MEMBER"]
            for name in ("_memory_a", "_memory_b"):
                for actors in (initial, hidden, updated):
                    check_path(actors[name])
                check(initial[name]["los"] == "1" and hidden[name]["los"] == "0", str(hidden))
                check(initial[name]["members"] == "2" and updated[name]["members"] == "2", str(updated))
                check(int(hidden[name]["time"]) - int(hidden[name]["seen_time"]) > 7000, str(hidden))
                for key in ("seen_time", "seen", "group_time", "shared", "group", "group_wp", "member_wp", "path_cost"):
                    check(hidden[name][key] == initial[name][key], f"Hidden member lost its memory: {hidden}")
            a, b = updated["_memory_a"], updated["_memory_b"]
            check(hidden["_memory_a"]["target_wp"] != hidden["_memory_a"]["group_wp"],
                  "Hidden target did not leave the observed waypoint")
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
        elif case == "solo-switch":
            visible, tracked, same = (samples[phase] for phase in ("VISIBLE", "TRACK", "SAME_TARGET"))
            check(visible["los"] == "1" and tracked["los"] == same["los"] == "0", str(samples))
            check(tracked["enemy"] == visible["enemy"] and tracked["group"] == "-1", str(tracked))
            check(tracked["seen_time"] == visible["seen_time"], str(tracked))
            check(tracked["goal"] not in ("-1", tracked["enemy"]), str(tracked))
            check(point(tracked, "goal_pos") == point(visible, "seen"), str(tracked))
            for key in ("enemy", "seen_time", "seen", "goal", "goal_pos"):
                check(same[key] == tracked[key], f"Same-target assignment changed {key}: {same}")
            target = all_samples["SWITCHED"]["_memory_b"]
            for phase in ("SWITCHED", "SWITCH_HOLD"):
                switched = samples[phase]
                check(switched["enemy"] == target["ent"] != tracked["enemy"] and switched["los"] == "0", str(switched))
                check(switched["group"] == "-1" and int(switched["health"]) > 0, str(switched))
                check(switched["seen_time"] == "0" and point(switched, "seen") == (0, 0, 0), str(switched))
                check(switched["goal"] == "-1", f"Old-target memory goal survived target switch: {switched}")
        elif case.startswith("short-") or case == "solo":
            visible = samples["VISIBLE"]
            check(visible["los"] == "1" and point(visible, "seen") == point(visible, "target"), str(visible))
            for phase in ("SHORT_B", "SHORT_C", "TRACK"):
                hidden = samples[phase]
                check(int(hidden["health"]) > 0, f"Memory actor died during {phase}: {hidden}")
                if phase != "TRACK":
                    check(math.dist(point(hidden, "pos"), point(visible, "pos")) < 1,
                          f"Memory actor moved during hold: {hidden}")
                check(hidden["los"] == "0" and hidden["enemy"] == visible["enemy"], str(hidden))
                check(0 < int(hidden["time"]) - int(hidden["seen_time"]) < 7000, str(hidden))
                for key in ("seen_time", "seen"):
                    check(hidden[key] == visible[key], f"Hidden movement changed {key}: {hidden}")
                if case == "solo":
                    check(hidden["group"] == "-1", str(hidden))
                else:
                    for key in ("group", "group_time", "shared", "clear_time"):
                        check(hidden[key] == visible[key], f"Hidden movement changed {key}: {hidden}")
                    if phase != "TRACK":
                        check_path(hidden)
                        for key in ("group_wp", "member_wp", "path_cost"):
                            check(hidden[key] == samples["SHORT_B"][key], f"Hidden movement changed routing: {hidden}")
            check(math.dist(point(samples["SHORT_B"], "target"), point(samples["SHORT_C"], "target")) > 100,
                  "Hidden positions did not change")
            if case != "solo":
                check(any(samples[phase]["target_wp"] != samples[phase]["group_wp"] for phase in ("SHORT_B", "SHORT_C")),
                      "Hidden target did not leave the observed waypoint")
            held_log = text.split("OJK_MEMORY_LOSS_BEGIN", 1)[1].split("OJK_MEMORY_PURSUIT", 1)[0]
            faces = [dict(word.split("=", 1) for word in line.split("squad ", 1)[1].split())
                     for line in held_log.splitlines() if "squad event=memory_face " in line]
            faces = [face for face in faces if face["ent"] == visible["ent"]]
            check(faces, "No hidden-target facing decisions")
            expected_yaw = math.degrees(math.atan2(point(visible, "seen")[1] - point(visible, "pos")[1],
                                                  point(visible, "seen")[0] - point(visible, "pos")[0]))
            for face in faces:
                check(point(face, "pos") == point(visible, "seen"), f"Facing followed hidden target: {face}")
                # Head position can differ from the actor origin.
                check(abs((float(face["yaw"]) - expected_yaw + 180) % 360 - 180) < 5, str(face))
            if case == "solo":
                tracked = samples["TRACK"]
                check(tracked["goal"] not in ("-1", tracked["enemy"]), str(tracked))
                check(point(tracked, "goal_pos") == point(visible, "seen"), f"Solo goal followed hidden target: {tracked}")
            reacquired = samples["REACQUIRED"]
            check(int(reacquired["health"]) > 0, f"Memory actor died before reacquisition: {reacquired}")
            check(reacquired["los"] == "1" and int(reacquired["seen_time"]) > int(samples["TRACK"]["time"]), str(reacquired))
            check(point(reacquired, "seen") == point(reacquired, "target"), str(reacquired))
        elif case in ("unseen", "solo-unseen"):
            unseen = samples["UNSEEN"]
            check(unseen["enemy"] == "0" and unseen["los"] == "0" and unseen["pvs"] == "1", str(unseen))
            check(unseen["seen_time"] == "0" and unseen["group_time"] == "0" and unseen["group"] == "-1",
                  f"Unseen enemy fabricated sight or a group: {unseen}")
            if case == "solo-unseen":
                chased = samples["UNSEEN_CHASE"]
                check(chased["los"] == "0" and chased["enemy"] == "0", str(chased))
                check(chased["goal"] == "-1" and chased["seen_time"] == "0" and chased["group"] == "-1", str(chased))
                check("event=memory_face" not in text, "Unseen target supplied a facing position")
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
