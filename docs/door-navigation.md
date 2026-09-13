# Door Navigation

## Intended Behaviour

Ordinary automatic doors can be used by NPCs. Player-only, button-use, Force-use,
locked, inactive, team-restricted, or named-NPC triggers can limit access.
Ranged enemies can also stop when they regain a clear shot through an open door;
they are not required to enter melee range.

## Confirmed Defect

Kril'dor's main hangar door, `Hanger_door1` in `t2_wedge`, has separate player and
NPC triggers on both sides. Its first listed trigger is player-only. Navigation
stored that first controller and rejected the closed door, despite usable NPC
triggers. A baseline probe stayed outside and timed out.

The validator now looks for another eligible automatic controller when the
cached one cannot serve the actor. It checks activity, button/player restrictions,
team, and named-NPC restrictions, including when reusing a cached controller.
It does not activate triggers, unlock doors, or bypass collision. Actual movement
still has to touch a valid trigger. The controller search does not establish that
every one-sided or distant trigger is reachable from the actor's approach side.

## Verification

```bash
python3 scripts/test-doors-sp.py
python3 scripts/test-ai-memory.py --case door
```

Four locomotion cases pass: main-hangar entry, main-hangar exit, an ordinary
central-building automatic door, and a locked-door control. The positive cases
require an initially closed door, observed opening, and a measured arrival on
the other side. The locked control must stay closed and time out.

The memory case lets a real stormtrooper see the player inside the hangar, holds
movement until the door closes and contact expires, then permits pursuit. It
requires approach into trigger range, restored line of sight, and refreshed
shared memory. It does not require the ranged NPC to keep moving once it can
shoot again. These are fresh, cleared test sessions, not campaign playthroughs.

The recent remembered-position precheck can still expose imperfect navigation
attachment near door thresholds. The verified hangar case did not require that
precheck to be removed. Record the map, door location, whether it was open, and
whether the enemy still had a firing line when reporting another refusal.

Use `nav doors [targetname]` to print door state. Use
`scripts/inspect-map.py --classes func_door --bounds t2_wedge` for original brush
bounds, or add `--match Hanger_door1` to inspect matching controllers. These bounds
are model-local data, not current moving-door collision bounds.
