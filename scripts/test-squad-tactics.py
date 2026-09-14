#!/usr/bin/env python3
"""Verify reports, flanks, regrouping, cover cycles, gait, and cleanup."""

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
             "contact-async": ("contact", 1), "contact-sync": ("contact", 0),
             "contact-hold": ("contact-hold", 1),
             "cycle-async": ("cycle", 1), "cycle-sync": ("cycle", 0),
             "cycle-cancel": ("cycle-cancel", 1),
             "regroup": ("regroup", 1), "solo": ("cycle-solo", 1), "cancel": ("cancel", 1), "save": ("save", 1),
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
        contact_samples, history = [], []
        for order, line in enumerate(text.splitlines()):
            line = re.sub(r"\^[0-9]", "", line)
            label = re.search(r"OJK_SQUAD_([A-Z_]+)$", line)
            if label:
                phase = label[1]
            if "aimemory event=sample " in line:
                sample: dict = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
                sample.update(phase=phase, order=order)
                history.append(sample)
                samples.setdefault(phase, {})[sample["name"]] = sample
                if phase == "CONTACT":
                    contact_samples.append(sample)
            elif "aimemory event=lifecycle " in line:
                state = dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split())
                samples[phase][state["name"]].update(state)
            elif any(f"aimemory event={kind} " in line for kind in ("reservation", "reuse", "cp")):
                controls.append(dict(word.split("=", 1) for word in line.split("aimemory ", 1)[1].split()))
            if "squad event=" in line:
                event: dict = dict(word.split("=", 1) for word in line.split("squad ", 1)[1].split() if "=" in word)
                event["phase"] = phase
                event["order"] = order
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
        elif case.startswith("contact-"):
            baseline, prehit, armed, hit, released = (samples[p]["_memory_a"] for p in
                                                      ("BASELINE", "PREHIT", "ARMED", "HIT", "RELEASED"))
            buddy = samples["ARMED"]["_memory_b"]
            check(math.dist(point(baseline), point(prehit)) < 4 and prehit["role"] == "0"
                  and prehit["los"] == "1", f"Invalid stationary baseline: {prehit}")
            check(armed["role"] == buddy["role"] == "0" and armed["group"] == buddy["group"] != "-1"
                  and armed["enemy"] == buddy["enemy"] == "0" and int(armed["members"]) >= 2,
                  f"Actors not ready: {armed}, {buddy}")
            check(buddy["health"] == buddy["max_health"] and buddy["weapon"] != "0"
                  and buddy["chase"] == "1" and buddy["dont_fire"] == "0"
                  and math.dist(point(armed), point(buddy)) <= 512, f"Buddy unavailable: {buddy}")
            check(armed["health"] == armed["max_health"] == hit["max_health"]
                  and int(armed["health"]) > int(hit["health"]) > int(hit["max_health"]) / 2,
                  f"Hit did not damage a healthy actor: {armed} -> {hit}")
            check(len(contact_samples) == 20 and all(s["health"] == hit["health"]
                  and s["max_health"] == armed["max_health"] for s in contact_samples), "Unexpected later damage")
            actor_events = [e for e in events if e.get("ent") == armed["ent"]]
            contacts = [e for e in actor_events if e["event"] == "contact_cover"]
            if case == "contact-hold":
                check(armed["chase"] == "0" and not contacts, "No-chase actor selected contact cover")
                check(not any(e["event"] == "tactic_assign" for e in actor_events), "No-chase actor received a role")
                check(all(s["role"] == "0" and math.dist(point(s), point(prehit)) < 4
                          for s in [*contact_samples, released]), "No-chase actor moved")
            else:
                check(armed["chase"] == "1" and armed["dont_fire"] == "0", str(armed))
                check(len(contacts) == 1 and contacts[0]["moving"] == "1" and int(contacts[0]["cp"]) >= 0,
                      f"No moving contact cover: {contacts}")
                cp = contacts[0]["cp"]
                retreats = [s for s in contact_samples if s["role"] == "1"]
                movement = [s for s in retreats if float(s["speed"]) > 0
                            and (int(s["forward"]) or int(s["right"]))]
                check(movement and all(s["walking"] == "0" for s in movement),
                      f"Retreat has no running movement: {retreats}")
                check(any(0 < float(s["walkSpeed"]) < float(s["speed"])
                          and float(s["runSpeed"]) > float(s["walkSpeed"]) for s in movement),
                      f"Retreat never exceeds walk speed on the open path: {movement}")
                covered = [s for s in contact_samples if s["role"] == "2" and s["cp"] == cp]
                check(retreats and len(covered) >= 2, f"Missing retreat or hold samples: {contact_samples}")
                moving = retreats[0]
                check(moving["cp"] == moving["combat_cp"] == cp and moving["occupied"] == "1"
                      and 0 < int(moving["deadline"]) - int(hit["time"]) <= 7000, str(moving))
                check(all(s["los"] == "0" and s["crouched"] == "1" and s["occupied"] == "1"
                          and s["combat_cp"] == cp and math.dist(point(s), point(moving, "tactic_goal")) < 24
                          for s in covered), f"No crouched hold in blocked cover: {covered}")
                check(math.dist(point(covered[0]), point(prehit)) >= 32, "Contact actor did not move to cover")
                check(0 < int(covered[0]["deadline"]) - int(covered[0]["time"]) <= 3000
                      and int(covered[0]["deadline"]) - int(hit["time"]) <= 10000, "Unbounded cover hold")
                check(any(e["event"] == "tactic_arrival" and e["cp"] == cp for e in actor_events), "No cover arrival")
                check(any(e["event"] == "tactic_finish" and e["role"] == "2" and e["cp"] == cp
                          and e["reason"] == "arrival" for e in actor_events), "Cover did not expire normally")
                check(any(e["event"] == "cp_release" and e["cp"] == cp and e["phase"] == "CONTACT"
                          for e in events), "Cover reservation not released")
                check(any(s["role"] == "0" and s["cp"] == s["combat_cp"] == "-1" and s["occupied"] == "0"
                          and 0 <= int(s["time"]) - int(covered[0]["deadline"]) <= 800
                          for s in contact_samples), "Contact cover not released within bounds")
                assignments = [e for e in actor_events if e["event"] == "tactic_assign"]
                check([e["role"] for e in assignments[:2]] == ["1", "2"], "Contact did not select retreat then hold")
        elif case.startswith("cycle-") or case == "solo":
            ready = samples["READY"]["_memory_a"]
            states = [s for s in history if s["name"] == "_memory_a" and s["order"] >= ready["order"]]
            actor_events = [e for e in events if e.get("ent") == ready["ent"]
                            and ready["order"] < e["order"] < states[-1]["order"]]
            check(ready["role"] == "0" and ready["los"] == "1" and ready["chase"] == "1"
                  and ready["dont_fire"] == "0", f"Invalid cycle baseline: {ready}")
            check(all(s["health"] == s["max_health"] == ready["health"] for s in states), "Cycle actor took damage")
            check(all(int(s["members"]) <= 1 for s in states if int(s["time"]) - int(ready["time"]) >= 1000),
                  "Cycle actor has support")
            check(not any(e["event"] == "tactic_assign" and e["role"] in ("3", "4", "5")
                          for e in actor_events), "Unsupported actor received a flank role")
            starts = [e for e in actor_events if e["event"] == "exposure_cover"]
            check(starts, "Healthy unsupported actor did not request cover")
            initial = [s for s in states if s["order"] < starts[0]["order"]]
            check(len(initial) >= 3 and all(s["role"] == "0" and s["los"] == "1"
                  and float(s["speed"]) < 4 and math.dist(point(s), point(ready)) < 4 for s in initial),
                  f"No stationary exposure before cover: {initial}")
            check(any(e["event"] == "fire_attempt" and e["role"] == "0"
                      and e["order"] < starts[0]["order"] for e in actor_events), "No firing before cover")
            check(min(int(s["time"]) for s in states if s["order"] > starts[0]["order"])
                  - int(ready["time"]) >= 2500, "Cover started before the exposure delay")
            # fire_attempt means an attack command, not a confirmed shot.
            check(not any(e["event"] == "fire_attempt" and e["role"] == "2" for e in actor_events),
                  "Actor tried to fire in cover")
            if case == "solo":
                check(samples["SOLO"]["_memory_a"]["role"] == "0", "Solo actor retreated immediately")
            if case == "cycle-cancel":
                before = samples["PREPARED"]["_memory_a"]
                check(before["role"] in ("1", "2"), f"Cancellation missed active cover: {before}")
                for phase in ("CLEANED", "LATER"):
                    state = samples[phase]["_memory_a"]
                    check(state["role"] == "0" and state["cp"] == state["combat_cp"] == "-1"
                          and state["occupied"] == "0" and state["speech"] == "0"
                          and float(state["speech_chance"]) == 0, f"Stale cycle state: {state}")
                check(any(e["event"] == "tactic_finish" and e.get("reason") == "disabled"
                          and e["phase"] == "CANCEL" for e in actor_events), "No disabled cleanup")
                if int(before["cp"]) >= 0:
                    check(before["combat_cp"] == before["cp"] and before["occupied"] == "1", str(before))
                    check(any(e["event"] == "cp_release" and e["cp"] == before["cp"]
                              and e["phase"] == "CANCEL" for e in events), "Cycle reservation not released")
                check(not any(e["event"] in ("tactic_assign", "cover_return", "exposure_cover", "movement_speech_consume")
                              and e["order"] > before["order"] for e in actor_events), "Cancelled cycle resumed")
            else:
                check(25000 <= int(states[-1]["time"]) - int(ready["time"]) <= 35000,
                      "Cycle observation is not 25 to 35 simulated seconds")
                completed = []
                for index, start in enumerate(starts):
                    end = starts[index+1]["order"] if index+1 < len(starts) else float("inf")
                    window = [e for e in actor_events if start["order"] < e["order"] < end]
                    finishes = [e for e in window if e["event"] == "tactic_finish" and e.get("reason") == "firing_position"]
                    if not finishes:
                        check(index == len(starts)-1, "Cycle restarted without a firing-position arrival")
                        continue
                    finish = finishes[0]
                    returns = [e for e in window if e["event"] == "cover_return" and e["order"] < finish["order"]]
                    check(len(returns) == 1, f"Missing cover return: {window}")
                    returning = returns[0]
                    for begin, stop in ((start, returning), (returning, finish)):
                        moving = [s for s in states if begin["order"] < s["order"] < stop["order"] and s["role"] == "1"]
                        check(moving and any(float(s["speed"]) > 0 for s in moving), "Missing cycle movement")
                        check(all(s["cp"] == begin["cp"] and s["combat_cp"] == s["cp"]
                                  and s["occupied"] == str(int(int(s["cp"]) >= 0)) for s in moving),
                              f"Invalid cycle reservation: {moving}")
                    held = [s for s in states if start["order"] < s["order"] < returning["order"] and s["role"] == "2"]
                    check(len(held) >= 4, f"Missing sustained cover hold: {held}")
                    check(all(s["cp"] == start["cp"] and s["crouched"] == "1" and s["los"] == "0"
                              and math.dist(point(s), point(s, "tactic_goal")) < 24
                              and s["combat_cp"] == s["cp"] and s["occupied"] == str(int(int(s["cp"]) >= 0))
                              for s in held), f"Invalid cover arrival: {held}")
                    check(int(held[-1]["time"]) - int(held[0]["time"]) >= 2000
                          and 0 < int(held[0]["deadline"]) - int(held[0]["time"]) <= 3000,
                          "Cover hold is not bounded to 3 seconds")
                    arrivals = [e for e in window if e["event"] == "tactic_arrival"]
                    check(any(e["cp"] == start["cp"] and e["order"] < held[0]["order"] for e in arrivals)
                          and any(e["cp"] == returning["cp"] and returning["order"] < e["order"] < finish["order"]
                                  for e in arrivals), "Missing physical cover or firing-position arrival")
                    check(any(e["event"] == "tactic_finish" and e["role"] == "2" and e.get("reason") == "arrival"
                              and held[-1]["order"] < e["order"] < returning["order"] for e in window),
                          "Cover hold did not finish normally")
                    check(any(e["event"] == "fire_attempt" and e["role"] == "0" and e["order"] > finish["order"]
                              for e in window), "No renewed fire after return")
                    if index+1 < len(starts):
                        # These samples bound events that have no time field.
                        before = max(int(s["time"]) for s in states if s["order"] < finish["order"])
                        after = min(int(s["time"]) for s in states if s["order"] > end)
                        check(after - before >= 3000, "Cycle restarted before the retry delay")
                    completed.append(finish)
                check(len(completed) >= 2, f"Expected two complete cover cycles, got {len(completed)}")
        elif case == "regroup":
            moving = samples["RETREAT"]["_memory_a"]
            check(moving["role"] in ("1", "2"), str(moving))
            check(not any(e["event"] == "tactic_assign" and e["role"] == "3" for e in events), "Unsupported flank")
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
