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
             "pressure": ("pressure", 1),
             "peek-pressure": ("peek-pressure", 1),
             "peek-save": ("peek-save", 1),
             "saber-near-async": ("saber-near", 1), "saber-near-sync": ("saber-near", 0),
             "saber-controls": ("saber-controls", 1),
             "saber-close": ("saber-close", 1),
             **{name: (name, 1) for name in ("held-shot", "held-damage", "held-saber", "held-noflee", "held-cinematic", "held-save")},
             "sour-merc": ("sour-probe", 1),
             "merge-plan": ("merge-plan", 1), "route-recovery": ("route-recovery", 1),
             **{name: (name, 1) for name in ("mixed-sith", "sith-squad", "mixed-sniper", "mixed-droid")},
             **{name: (name, 1) for name in ("sour-rodian", "sour-trandoshan", "sour-weequay", "sour-sniper", "sour-shot", "sour-saber")},
             "pressure-cooldown-async": ("pressure-cooldown", 1), "pressure-cooldown-sync": ("pressure-cooldown", 0),
             **{name: (name, 1) for name in ("pressure-radius", "pressure-support", "pressure-contested", "handoff", "handoff-unavailable",
                                            "cover-reassess", "cover-hidden", "save-outward", "save-withdrawal")},
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
        map_name = "t1_sour" if case.startswith("sour-") else "t2_wedge"
        environment = dict(os.environ, OJK_SMOKE_ROOT=str(suite / case))
        if map_name == "t1_sour":
            environment.setdefault("OJK_SMOKE_TIMEOUT", "240")
        subprocess.run(["bash", str(root / "scripts/smoke-sp.sh"), str(args.package.resolve()), map_name,
                        "+exec", "ai-squad-settings.cfg", *mode,
                        "+exec", f"ai-squad-{fixture}.cfg"],
                       env=environment, check=True)
        logs = list((suite / case).glob(f"{map_name}.*/console.log"))
        check(len(logs) == 1, f"Missing log: {suite / case}")
        text = logs[0].read_text(errors="replace")
        check("aimemory event=rejected" not in text, f"Rejected fixture control: {logs[0]}")
        check(not re.search(r"couldn't exec|Unknown command|ERROR:", text), f"Fixture error: {logs[0]}")
        samples, events, controls, phase = {}, [], [], ""
        contact_samples, history = [], []
        for order, line in enumerate(text.splitlines()):
            line = re.sub(r"\^[0-9]", "", line)
            label = re.search(r"OJK_(?:SQUAD|SOUR)_([A-Z_]+)$", line)
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
        if case.startswith("sour-"):
            before = next(iter(samples["BEFORE"].values()))
            states = [s for s in history if s["ent"] == before["ent"] and s["phase"] == "HIT"]
            final = next(iter(samples["DONE"].values()))
            expected = {"sour-rodian": "rodian2", "sour-trandoshan": "trandoshan", "sour-weequay": "weequay",
                        "sour-sniper": "rodian"}.get(case, "human_merc")
            check(before["type"].lower().startswith(expected) and before["enemy"] == "0" and before["scripted"] == "0", str(before))
            check("native_selected" in text and int(before["max_health"]) >= 20, "Missing native actor")
            if case != "sour-weequay":
                check(before["spawn_script"].startswith("t1_sour/") and before["chase"] == "0" and before["no_flee"] == "1"
                      and before["crouched"] == "1", "Native hold script did not run")
            if case == "sour-sniper":
                check(before["weapon"] == "4", "Sniper control is not using a disruptor")
            moving = [s for s in states if s["role"] == "1" and float(s["speed"]) > float(s["walkSpeed"])]
            check(moving and any(s["crouched"] == s["walking"] == "0" for s in moving), "Native actor did not run standing")
            check(max(math.dist(point(before), point(s)) for s in states) >= 32, "Native actor remained at its authored position")
            if case != "sour-weequay":
                check(all(s["script_flags"] == before["script_flags"] for s in [*states, final]), "Native script flags were overwritten")
                check(final["pressure_move"] == "0" and final["crouched"] == "1", "Native hold stance did not resume")
            if case in ("sour-shot", "sour-saber"):
                check(all(s["health"] == before["health"] for s in states), "Native pressure needed damage")
                source_event = "incoming_fire" if case == "sour-shot" else "saber_pressure"
                check(any(e["event"] == source_event and e["ent"] == before["ent"] for e in events), "Native pressure source missing")
            else:
                check(states and 0 < int(states[0]["health"]) < int(before["health"]), "Native damage did not reach the pain handler")
        elif case == "merge-plan":
            before, after = samples["SPLIT"], samples["MERGED"]
            check(before["_memory_a"]["group"] == before["_memory_b"]["group"] != before["_memory_c"]["group"], "Groups were not separate")
            check(math.dist(point(before["_memory_a"]), point(before["_memory_c"])) > 256, "Commanders are within contact range")
            check(len({s["group"] for s in after.values()}) == 1, "Member contact did not merge groups")
            for name in ("_memory_a", "_memory_b"):
                check(before[name]["role"] in ("3", "5"), "No active plan to preserve")
                for key in ("role", "tactic_goal", "tactic_threat", "deadline", "cp"):
                    check(after[name][key] == before[name][key], f"Merge changed {name}.{key}")
            check(any(e["event"] == "group_merge" for e in events), "No merge evidence")
        elif case == "route-recovery":
            before = samples["BLOCK_READY"]["_memory_a"]
            check(before["role"] == "1", "Route was not active before blocking")
            failures = [e for e in events if e["event"] == "tactic_finish" and e["ent"] == before["ent"]
                        and e["phase"] == "BLOCKED" and e.get("reason") in ("route_blocked", "route_failed")]
            moves = [e for e in events if e["event"] == "pressure_cover" and e["phase"] == "BLOCKED"]
            check(failures, "Blocked route did not trigger recovery")
            check(any(s["phase"] == "BLOCKED" and s["order"] > failures[0]["order"]
                      and int(s["time"]) < int(before["deadline"]) and s["combat_cp"] == "-1"
                      for s in history), "Blocked route retained its cover claim until timeout")
            if moves:
                check(math.dist(point(moves[0], "goal"), point(before, "tactic_goal")) >= 64, "Recovery selected the same blocked destination")
                check(any(s["phase"] == "RECOVERED" and math.dist(point(s), point(before)) >= 32 for s in history),
                      "No physical movement after the enclosure was removed")
            else:
                check(any(e["event"] == "pressure_response" and e["reason"] in ("concealed", "no_cover") and e["phase"] == "BLOCKED" for e in events)
                      and any(s["phase"] == "BLOCKED" and s["role"] == "0" and s["goal"] == "-1" for s in history),
                      "Recovery neither replanned nor held after checking for cover")
        elif case in ("mixed-sith", "sith-squad", "mixed-sniper", "mixed-droid"):
            a, b = (samples["JOINED"][name] for name in ("_memory_a", "_memory_b"))
            check(a["group"] == b["group"] != "-1" and a["enemy"] == b["enemy"] == "0", str(samples["JOINED"]))
            if case == "mixed-droid":
                check(b["type"] == "sentry", "Mixed recipient is not a sentry")
            else:
                check(b["weapon"] == ("4" if case == "mixed-sniper" else "1"), "Wrong mixed-squad recipient")
            if case == "sith-squad":
                check(a["weapon"] == "1", "Sith source is not using a saber")
            check(a["role"] == b["role"] == "0", "Membership assigned a trooper movement role")
            check(any(e["event"] == "report_delivery" and e["recipient"] == b["ent"] for e in events), "No mixed-squad report")
            if case == "mixed-sniper":
                check(b["seen_time"] == "0" and b["los"] == "0", "Hidden sniper received invented sight")
        elif case == "recruit":
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
            contacts = [e for e in actor_events if e["event"] in ("contact_cover", "pressure_cover")]
            if case == "contact-hold":
                check(armed["chase"] == "0" and not contacts, "No-chase actor selected contact cover")
                check(not any(e["event"] == "tactic_assign" for e in actor_events), "No-chase actor received a role")
                check(all(s["role"] == "0" and math.dist(point(s), point(prehit)) < 4
                          for s in [*contact_samples, released]), "No-chase actor moved")
            else:
                check(armed["chase"] == "1" and armed["dont_fire"] == "0", str(armed))
                check(contacts and contacts[0].get("moving", "1") == "1" and int(contacts[0]["cp"]) >= -1,
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
                covered = [s for s in contact_samples if s["role"] == "2" and s["cp"] == cp and s["peek"] == "0"]
                check(retreats and len(covered) >= 2, f"Missing retreat or hold samples: {contact_samples}")
                moving = retreats[0]
                occupied = "1" if int(cp) >= 0 else "0"
                check(moving["cp"] == moving["combat_cp"] == cp and moving["occupied"] == occupied
                      and 0 < int(moving["deadline"]) - int(hit["time"]) <= 7000, str(moving))
                check(all(s["los"] == "0" and s["crouched"] == "1" and s["occupied"] == occupied
                          and s["combat_cp"] == cp and math.dist(point(s), point(moving, "tactic_goal")) < 24
                          for s in covered), f"No crouched hold in blocked cover: {covered}")
                check(math.dist(point(covered[0]), point(prehit)) >= 32, "Contact actor did not move to cover")
                check(0 < int(covered[0]["deadline"]) - int(covered[0]["time"]) <= 4000
                      and int(covered[0]["deadline"]) - int(hit["time"]) <= 10000, "Unbounded cover hold")
                check(any(e["event"] == "tactic_arrival" and e["cp"] == cp for e in actor_events), "No cover arrival")
                # Damage now uses the pressure cycle; reservation release is checked by the cycle/lifecycle cases.
                assignments = [e for e in actor_events if e["event"] == "tactic_assign"]
                check([e["role"] for e in assignments[:2]] == ["1", "2"], "Contact did not select retreat then hold")
        elif case == "held-save":
            before, after, final = (samples[p]["_memory_a"] for p in ("SAVE", "RESTORED", "HELD_FINISHED"))
            check(before["role"] == before["pressure_move"] == "1" and before["chase"] == "0", "No active hold override to save")
            for key in ("role", "pressure_move", "chase", "no_flee", "script_flags", "cp", "combat_cp", "deadline", "tactic_goal", "tactic_threat"):
                check(before[key] == after[key], f"Save changed override {key}")
            check(final["role"] == final["pressure_move"] == "0" and final["script_flags"] == before["script_flags"],
                  "Loaded override did not restore the hold order")
            check(any(e["event"] == "tactic_arrival" and e["phase"] == "RESTORED" for e in events), "Loaded retreat did not arrive")
        elif case.startswith("held-"):
            before, hit, final = (samples[p]["_memory_a"] for p in ("HELD_READY", "HELD_HIT", "HELD_FINISHED"))
            states = [s for s in history if s["name"] == "_memory_a" and s["phase"] == "HELD_RESPONSE"]
            actor_events = [e for e in events if e.get("ent") == before["ent"]]
            moves = [e for e in actor_events if e["event"] == "pressure_cover"]
            check(all(s["health"] == hit["health"] for s in states), "Unexpected extra damage")
            if case in ("held-damage", "held-cinematic"):
                check(int(hit["health"]) < int(before["health"]), "Damage control did not hit")
            else:
                check(hit["health"] == before["health"], "Pressure control changed health")
            if case == "held-cinematic":
                check(not moves and all(s["role"] == "0" and s["pressure_move"] == "0" for s in states),
                      "Pressure took control of a cinematic actor")
                check(all(s["goal"] in (before["goal"], "-1") and s["behavior"] == before["behavior"] for s in states),
                      "Pressure replaced a script goal")
            else:
                check(moves and moves[0]["override"] == "1", "Hold order blocked pressure movement")
                check(any(s["role"] == "1" and s["pressure_move"] == "1" and float(s["speed"]) > float(s["walkSpeed"])
                          for s in states), "Held actor did not run")
                check(math.dist(point(before), point(final)) >= 32, "Held actor did not change position")
                for state in [*states, final]:
                    for key in ("chase", "no_flee", "dont_fire", "script_flags"):
                        check(state[key] == before[key], f"Pressure changed the original {key} order")
                check(final["role"] == "0" and final["pressure_move"] == "0", "Temporary movement override did not end")
                check(not any(e["event"] == "tactic_assign" and e["role"] in ("3", "4", "5") for e in actor_events),
                      "Held actor received an offensive role")
                if case == "held-saber":
                    check(any(e["event"] == "saber_pressure" for e in actor_events), "No saber pressure")
                elif case != "held-damage":
                    check(any(e["event"] == "incoming_fire" for e in actor_events), "No shot pressure")
                check(before["no_flee"] == "1" if case == "held-noflee" else before["chase"] == "0", "No restrictive order")
        elif case.startswith("saber-near") or case == "saber-close":
            ready = samples["SABER_READY"]["_memory_a"]
            check(ready["weapon"] == "3" and ready["enemy_weapon"] == ready["enemy_saber"] == "1"
                  and ready["los"] == "1" and int(ready["retry"]) >= 9000
                  and math.dist(point(ready), point(ready, "target")) < 192, str(ready))
            sources = [e for e in events if e["event"] == "saber_pressure" and e["ent"] == ready["ent"]]
            moves = [e for e in events if e["event"] == "pressure_cover" and e["ent"] == ready["ent"]]
            check(sources and moves and 0 <= int(moves[0]["time"])-int(sources[0]["time"]) <= 250,
                  "Visible saber did not cause a prompt retreat")
            check(not any(e["event"] == "incoming_fire" and e["ent"] == ready["ent"]
                          and e["order"] < moves[0]["order"] for e in events), "A missile caused the initial retreat")
            states = [s for s in history if s["name"] == "_memory_a" and s["phase"] == "SABER_NEAR"]
            check(all(s["health"] == ready["health"] for s in states), "Saber pressure required damage")
            if case == "saber-close":
                initial_distance = math.dist(point(ready), point(ready, "target"))
                check(initial_distance < 128, "Close case is outside the navigation danger radius")
                check(all(math.dist(point(s), point(s, "target")) >= initial_distance-4 for s in states),
                      "Close escape moved toward the saber")
            check(any(s["role"] == "1" and s["walking"] == "0" and float(s["speed"]) > float(s["walkSpeed"])
                      for s in states), "No running saber retreat")
            if moves[0]["escape"] == "1":
                check(math.dist(point(moves[0], "goal"), point(moves[0], "knownposition"))
                      >= math.dist(point(ready), point(ready, "target"))+64, "Escape did not gain distance")
                check(any(math.dist(point(s), point(moves[0], "goal")) < 24 for s in states)
                      and any(e["event"] == "tactic_finish" and e.get("reason") == "firing_position"
                              and e["ent"] == ready["ent"] for e in events),
                      "Saber escape did not arrive")
            else:
                check(any(s["role"] == "2" and s["los"] == "0" and math.dist(point(s), point(moves[0], "goal")) < 24
                          for s in states), "Saber retreat did not reach cover")
        elif case == "saber-controls":
            for phase in ("GUN", "OFF", "FAR", "DECAY", "DISABLED", "MELEE", "HIDDEN"):
                state = samples[phase]["_memory_a"]
                check(state["pressure"] == "0" and state["health"] == state["max_health"], str(state))
            check(samples["GUN"]["_memory_a"]["enemy_weapon"] == "3", "Gun control has a saber")
            check(samples["OFF"]["_memory_a"]["enemy_weapon"] == "1" and samples["OFF"]["_memory_a"]["enemy_saber"] == "0",
                  "Saber-off control has an active blade")
            far = samples["FAR"]["_memory_a"]
            check(far["enemy_saber"] == "1" and far["los"] == "1" and math.dist(point(far), point(far, "target")) > 192,
                  "Far control is not visible and outside the radius")
            held = samples["HELD"]["_memory_a"]
            check(held["pressure"] == "1" and held["chase"] == "0" and held["role"] == "0"
                  and math.dist(point(held), point(far)) < 4, "Saber pressure overrode no-chase")
            melee = samples["MELEE"]["_memory_a"]
            check(melee["weapon"] == "17" and melee["enemy_saber"] == "1" and melee["los"] == "1", "Invalid melee control")
            before, hidden = (samples[p]["_memory_a"] for p in ("HIDDEN_READY", "HIDDEN"))
            check(hidden["weapon"] == "3" and hidden["enemy_saber"] == "1" and hidden["los"] == "0"
                  and math.dist(point(hidden), point(hidden, "target")) < 512, "Hidden control is not inside the test radius")
            for key in ("enemy", "seen", "seen_time"):
                check(hidden[key] == before[key], f"Hidden saber changed {key}")
            check(not any(e["event"] == "saber_pressure" and int(e["time"]) >= int(samples["DECAY"]["_memory_a"]["time"])
                          for e in events), "Excluded or hidden saber refreshed pressure")
        elif case.startswith("pressure-cooldown") or case == "pressure-support":
            ready = samples["PRESSURED_READY"]["_memory_a"]
            states = [s for s in history if s["name"] == "_memory_a" and s["phase"] == "RESPONSE"]
            pressure = [e for e in events if e["event"] == "incoming_fire" and e["ent"] == ready["ent"]]
            moves = [e for e in events if e["event"] == "pressure_cover" and e["ent"] == ready["ent"]]
            check(pressure and moves and 0 <= int(moves[0]["time"])-int(pressure[0]["time"]) <= 250,
                  f"Pressure did not start a prompt retreat: {pressure}, {moves}")
            check(all(s["health"] == ready["health"] for s in states), "Pressure caused damage")
            check(any(s["role"] == "1" and float(s["speed"]) > float(s["walkSpeed"]) for s in states), "No running retreat")
            check(any(s["role"] == "2" and s["los"] == "0" and math.dist(point(s), point(moves[0], "goal")) < 24
                      for s in states), "Pressure retreat did not reach cover")
            if case.startswith("pressure-cooldown"):
                check(int(ready["retry"]) >= 9000 and moves[0]["cp"] == "-1", "Fixture did not require local cover through cooldown")
            else:
                buddy = samples["PRESSURED_READY"]["_memory_b"]
                check(any(e["event"] == "retreat_support" and e["ent"] == buddy["ent"] and e["mover"] == ready["ent"]
                          for e in events), "No retreat supporter")
                check(any(s["name"] == "_memory_b" and s["role"] == "5" and s["support"] == "1"
                          for s in history), "Supporter did not hold position")
                check(any(e["event"] == "fire_attempt" and e["ent"] == buddy["ent"] and e["role"] == "5"
                          for e in events), "Supporter did not fire")
        elif case == "pressure-contested":
            ready = samples["PRESSURED_READY"]
            claims = {}
            for event in events:
                if event["event"] == "pressure_cover" and event["ent"] in {s["ent"] for s in ready.values()}:
                    claims.setdefault(event["ent"], event)
            check(len(claims) == 2 and all(e["cp"] == "-1" for e in claims.values()), "Both actors did not use local cover")
            goals = [point(e, "goal") for e in claims.values()]
            check(math.dist(*goals) >= 48, "Actors selected overlapping local cover")
            for name, actor in ready.items():
                states = [s for s in history if s["name"] == name and s["phase"] == "RESPONSE"]
                check(all(s["health"] == actor["health"] for s in states), "Contested cover changed health")
                check(any(s["role"] == "1" and float(s["speed"]) > 0 for s in states), "Actor did not move toward its own cover")
                check(any(s["role"] == "2" and s["los"] == "0"
                          and math.dist(point(s), point(claims[actor["ent"]], "goal")) < 24 for s in states),
                      "Contested cover did not reach a concealed position")
        elif case == "pressure-radius":
            for phase, expected in (("SMALL", "0"), ("LARGE", "1"), ("DECAY", "0"), ("WALL", "0")):
                state = samples[phase]["_memory_wall" if phase == "WALL" else "_memory_a"]
                check(state["pressure"] == expected and state["health"] == state["max_health"], str(state))
            wall = samples["WALL"]["_memory_wall"]
            check(not any(e["event"] == "incoming_fire" and (e["phase"] == "SMALL" or (e["phase"] == "WALL" and e["ent"] == wall["ent"]))
                          for e in events), "Rejected shot caused pressure")
            check(any(e["event"] == "pressure_ignored" and e["reason"] == "wall" and e["phase"] == "WALL"
                      and e["ent"] == wall["ent"]
                      for e in events), "Wall control did not test shielding")
        elif case in ("handoff", "handoff-unavailable"):
            ready = samples["HANDOFF_READY"]
            a, b = ready["_memory_a"], ready["_memory_b"]
            check(a["role"] == "5" and b["role"] == "3", f"No established flank: {ready}")
            moves = [e for e in events if e["event"] == "pressure_cover" and e["ent"] == a["ent"]]
            check(moves, "Pressured supporter did not retreat")
            check(any(s["name"] == "_memory_a" and s["phase"] == "HANDOFF" and s["role"] == "1" for s in history), "Supporter stayed exposed")
            if case == "handoff":
                c = ready["_memory_c"]
                check(any(e["event"] == "support_handoff" and e["ent"] == a["ent"] and e["replacement"] == c["ent"]
                          and e["mover"] == b["ent"] for e in events), "No support handoff")
                check(samples["HANDOFF"]["_memory_b"]["role"] in ("3", "4")
                      and samples["HANDOFF"]["_memory_c"]["role"] == "5", str(samples["HANDOFF"]))
                check(not any(e["event"] == "tactic_finish" and e["ent"] == b["ent"] and e["phase"] == "HANDOFF"
                              and e["reason"] in ("support_pressure", "support_lost") for e in events), "Handoff cancelled the flank")
            else:
                check(any(e["event"] == "tactic_finish" and e["ent"] == b["ent"] and e["reason"] == "support_pressure"
                          for e in events), "Unsupported flank continued")
        elif case in ("cover-reassess", "cover-hidden"):
            before, moved = (samples[p]["_memory_a"] for p in ("BEFORE", "TARGET_MOVED"))
            check(point(before, "anchor") != (0, 0, 0) and before["role"] in ("1", "2"), "No active cover pair")
            check(before["seen"] == moved["seen"] and before["seen_time"] == moved["seen_time"], "Frozen move changed sight")
            if case == "cover-reassess":
                check(any(e["event"] == "tactic_finish" and e["reason"] == "cover_exposed" for e in events), "Exposed cover was retained")
                changes = [e for e in events if e["event"] == "pressure_cover" and e["ent"] == before["ent"]]
                observed = samples["REASSESS"]["_memory_a"]
                check(changes and math.dist(point(changes[0], "knownposition"), point(observed, "seen")) < 1, "Replan did not use confirmed new position")
                check(math.dist(point(changes[0], "goal"), point(observed, "seen")) >= 128, "Replan approached the threat")
            else:
                hidden = samples["HIDDEN"]["_memory_a"]
                check(hidden["los"] == "0", "Target was not hidden")
                for key in ("seen", "seen_time", "shared", "group_time"):
                    check(hidden[key] == before[key], f"Hidden movement changed {key}")
                check(all(math.dist(point(e, "knownposition"), point(before, "seen")) < 1
                          for e in events if e["event"] == "pressure_cover" and e["phase"] in ("TARGET_MOVED", "HIDDEN")),
                      "Replan used the hidden target position")
        elif case in ("peek-save", "save-outward", "save-withdrawal"):
            before, after = (samples[p]["_memory_a"] for p in ("SAVE", "RESTORED"))
            check(before["role"] in ("1", "2") and point(before, "anchor") != (0, 0, 0), str(before))
            check("Loaded saved game format 3" in text, "Wrong save version")
            if case != "peek-save":
                check(before["role"] == "1" and before["peek"] == ("1" if case == "save-outward" else "0"), "Captured the wrong movement phase")
            check(math.dist(point(before), point(after)) < 4, "Loaded actor moved away from cover")
            for key in ("role", "cp", "combat_cp", "occupied", "anchor", "peek", "deadline", "tactic_goal", "tactic_threat"):
                check(before[key] == after[key], f"Save changed {key}: {before} -> {after}")
            check(any(e["event"] in ("peek_move", "peek_withdraw") and e["phase"] == "RESTORED"
                      for e in events), "Loaded cover pair did not resume")
        elif case == "peek-pressure":
            ready = samples["READY"]["_memory_a"]
            actor_events = [e for e in events if e.get("ent") == ready["ent"] and e["order"] < history[-1]["order"]]
            threats = [e for e in actor_events if e["event"] == "incoming_fire"]
            withdrawals = [e for e in actor_events if e["event"] == "peek_withdraw" and e["pressure"] == "1"]
            check(threats and withdrawals, "No pressure-driven withdrawal")
            for event in withdrawals:
                previous = [e for e in threats if e["order"] < event["order"]]
                after = next((s for s in history if s["order"] > event["order"]), None)
                check(previous and after and int(after["time"])-int(previous[-1]["time"]) <= 1000,
                      "Pressure response took too long")
            check(all(s["health"] == ready["health"] for s in history), "Near miss caused damage")
            check(any(s["role"] == "2" and s["peek"] == "0" and s["los"] == "0"
                      and s["order"] > withdrawals[0]["order"] and math.dist(point(s), point(s, "anchor")) < 24
                      for s in history), "No return behind cover")
        elif case == "pressure":
            before = samples["PRESSURE_BASE"]["_memory_a"]
            for phase in ("FRIENDLY", "DISTANT", "NEAR", "DECAY"):
                state = samples[phase]["_memory_a"]
                check(state["health"] == before["health"] and state["los"] == "0", str(state))
                for key in ("enemy", "seen_time", "seen", "shared", "group_time"):
                    check(state[key] == before[key], f"Pressure changed memory: {key}: {state}")
                check(state["pressure"] == ("1" if phase == "NEAR" else "0"), str(state))
            pressure = [e for e in events if e["event"] == "incoming_fire" and e["ent"] == before["ent"]]
            check(len(pressure) == 1 and pressure[0]["phase"] == "DISTANT", str(pressure))
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
            check(not any(e["event"] == "fire_attempt" and e["role"] == "2" and e["peek"] == "0" for e in actor_events),
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
                check(int(states[-1]["time"]) - int(ready["time"]) >= 25000,
                      "Cycle observation is shorter than 25 simulated seconds")
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
                    held = [s for s in states if start["order"] < s["order"] < returning["order"] and s["role"] == "2" and s["peek"] == "0"]
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
                check(completed, "No complete cover cycle")
                peeks = [e for e in actor_events if e["event"] == "peek_move"]
                check(len(peeks) >= 2, "No repeated local peeks")
                check(all(math.dist(point(e, "anchor"), point(e, "goal")) <= 145 for e in peeks), str(peeks))
                check(any(e["event"] == "fire_attempt" and e["role"] == "2" and e["peek"] == "1" for e in actor_events), "No fire from a peek")
                for peek in peeks[:-1]:
                    withdrawal = next((e for e in actor_events if e["event"] == "peek_withdraw" and e["order"] > peek["order"]), None)
                    check(withdrawal and any(s["role"] == "2" and s["peek"] == "0"
                          and s["order"] > withdrawal["order"] and math.dist(point(s), point(peek, "anchor")) < 24
                          for s in states), "Peek did not return to its cover")
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
