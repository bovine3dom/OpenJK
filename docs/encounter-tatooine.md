# Tatooine Native AI Checks

The `t1_sour` checks use original map spawners, NPC definitions, and ICARUS
scripts. They do not replace the native enemies with diagnostic stormtroopers.
The fixture activates spawners with `use`, waits for their spawn scripts, and
lets the selected NPC acquire the player through normal perception.

## Findings

- `stay_put` and `stay_here` set crouch, disable chase, and prohibit fleeing.
- `ctrl_rodian` first runs to two navigation goals, then sets the same hold orders.
- Human mercenaries use the stormtrooper class. Rodians, Trandoshans, and Weequays use distinct classes.
- Rodians without the blaster spawn flag use a disruptor and the sniper controller.
- The old final script-flag pass reapplied crouching after retreat movement. The native mercenary therefore moved while crouched even with a running command.

The pressure override now applies through group cleanup, steering speed, and
final command generation. Native ranged classes use the pressure policy.
The sniper controller calls the shared pressure path and resumes sniping after
the retreat. General NPC pain handling records damage after target assignment.
Original script flags remain intact and take effect again after the retreat.

## Fixtures

Run from the repository root after a normal published build:

```bash
python3 scripts/test-squad-tactics.py --case sour-merc
python3 scripts/test-squad-tactics.py --case sour-rodian
python3 scripts/test-squad-tactics.py --case sour-trandoshan
python3 scripts/test-squad-tactics.py --case sour-weequay
python3 scripts/test-squad-tactics.py --case sour-sniper
python3 scripts/test-squad-tactics.py --case sour-shot
python3 scripts/test-squad-tactics.py --case sour-saber
```

The tests check native type, weapon, spawn script, enemy acquisition, damage,
physical movement, standing gait, and preserved script flags. The player and the
selected NPC are protected from incidental damage. The damage control applies
five damage through the normal damage handler. Production health is unchanged.

The tests activate selected encounters directly. They do not qualify the full
campaign sequence or every NPC placement in the map.

## Inspection

In a cheat-enabled session, `nav actors` lists live NPCs and their controller
state. `nav select rodian` selects the nearest live NPC of that type. A trailing
asterisk selects a type prefix, for example `nav select weequay*`.
`vstr nav_native_sample` prints its detailed state. `nav memory #147` reads a
specific entity slot without changing the NPC's target name. Select again after
a map change or entity replacement.
