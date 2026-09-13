# Kril'dor Route Test

## Selection

Use the ground-floor interior of the central building in `t2_wedge`. A central
obstacle separates two paths around the room. This supports a first test in which
one enemy engages while another moves around the opposite side.

The original map has 181 combat points, 244 waypoints, and four stormtrooper
spawners with `NPC_targetname` set to `npc_squad1` near this room. These are static
map counts, not a guarantee that four enemies will be active together. The
headless test observed a three-member group targeting the player, a successful
combat-point reservation, and goal completion. No extra enemies were spawned.

[IGN's Kril'dor guide](https://www.ign.com/wikis/star-wars-jedi-knight-jedi-academy/Guide_part_7)
identifies the connected walkways and central building with four doors. Map data
and the runtime graph, rather than the guide alone, support this selection.

Other areas considered:

- The Kril'dor hangar has enemies and combat points, but includes scripted movement and an elevator. The central interior gives a simpler local loop.
- Coruscant (`t2_rogue`) has 235 combat points, but its larger layout needs a separate route survey.
- [Dosuun](https://portforward.com/games/walkthroughs/Star-Wars-Jedi-Knight-Jedi-Academy/Cult-Investigation-Dosuun.htm) has locked-door objectives and removes the player's saber. Do not use it for the first anti-Jedi test.

## Reproduction

Build with `bash scripts/build-sp.sh`, then run:

```bash
bash scripts/smoke-sp.sh build/ready t2_wedge +exec krildor-route.cfg
```

The fixture skips the opening scene, enables player invulnerability, enables
`d_npcai 4`, and uses `setviewpos 2772 468 -60 180`. It then prints the local graph.
The final log marker is `OJK_KRILDOR_ROUTE_READY`. Start fresh; do not load a save.
`wait` is not an exact simulation-time delay. Do not compare exact event counts.

For a later desktop test, use this command inside a completed package:

```bash
bash launch-sp.sh /path/to/GameData +devmap t2_wedge +exec krildor-route.cfg
```

## Observed Routes

The runtime graph contains this inner loop at approximately Z = -104:

| Node | X | Y |
| --- | --- | --- |
| 244 | 2772 | 468 |
| 245 | 2668 | 404 |
| 246 | 2560 | 320 |
| 247 | 2432 | 384 |
| 248 | 2368 | 556 |
| 249 | 2452 | 620 |
| 250 | 2560 | 704 |
| 251 | 2688 | 640 |

Two paths between nodes 244 and 248 are:

- South side: `244 -> 245 -> 246 -> 247 -> 248`.
- North side: `244 -> 251 -> 250 -> 249 -> 248`.

All eight links reported cached `valid=1`, `jump=0`, `fly=0`, `blocking=0`,
`entity=1023` (no blocking entity), and `size=1` (medium clearance). The paths have
no shared intermediate node. Outer access links include doors; keep the first
experiment inside the room. Node numbers can change if map or graph generation
changes. Match coordinates as well as numbers.

## Traversal Tests

Run the separate locomotion suite after building:

```bash
python3 scripts/test-traversal-sp.py
python3 scripts/test-traversal-sp.py --case north
```

All eight cases passed headlessly:

| Case | Verified result |
| --- | --- |
| South | The probe reaches all four intermediate/final goals in order. |
| North | The probe reaches all four intermediate/final goals in order. |
| Direct | With only the final goal, the navigator selects a path around the pillar. The trace reports remaining path nodes before arrival. |
| Blocked | A goal inside the pillar times out without reporting arrival. |
| Cancel | A concurrent start is rejected. Cancellation removes the probe, and a new probe can complete. |
| Frozen | The per-frame watchdog times out even when NPC thinking is frozen. |
| Removed | Killing the probe reports failure and cleans up its actor and goal. |
| Save | Saving during traversal cancels and removes the probe before serialization. |

The suite runs cases sequentially, with separate profiles and logs under
`build/smoke/traversal.*`. A successful map smoke test alone does not pass a
traversal case. The suite also checks ordered arrivals, measured positions,
distance, cleanup, and the expected terminal event.

`krildor-traverse.cfg` is different from the original encounter fixture. It alerts
the local guards, kills native NPCs, shortens corpse retention, and moves the
player away while bodies are removed. It then places the observer at
`2772 560 -60` through `setviewpos`. Use it only in a fresh test session. It is not
a campaign-progression or combat test. This setup avoids occupied spawn points
and moving guards blocking the route.

The test-owned stormtrooper spawns at `(2772, 468, -90)` and settles at Z =
`-103.875`. It uses normal collision, steering, navigation, and NPC movement.
It does not teleport during traversal. Its cinematic behaviour prevents combat
decisions from replacing route goals. The probe is unarmed and excluded from
groups and enemy selection; it does not drop a weapon when killed. Health and
movement statistics are not increased.

Route goals use Z = `-103.875`. Arrival uses an eight-unit radius or containment
of the requested point in the actor's bounds. The suite requires a measured
distance below 24 units and a height difference below two units. A larger initial
tolerance caused premature turns and a stuck run. Native guards also caused
occupied-spawn failures. Keep those observations separate from clean-route results.

## Probe Command

```text
nav test <per-leg-ms> <spawn-x spawn-y spawn-z> <goal-x goal-y goal-z> ...
nav test cancel
```

This cheat command accepts two through eight points and a per-leg timeout of
100 through 30000 milliseconds. Point zero is the spawn location. The other
points are ordered goals. Only one probe can run at a time. The actor is named
`_route_test` for diagnostics. Success, timeout, cancellation, death, saving, and
shutdown remove the test actor and its temporary goal. Nothing runs when there
is no active probe. The probe does not reserve combat points.

Records start with `routetest event=`. Position samples include remaining path
nodes, a bounds trace toward the next goal, and geometric line of sight to the
player. Arrival is checked against the requested coordinate, not a replacement
script goal. Entity 1022 in a trace means world geometry; 1023 means no entity.

## Visibility and Limits

Both routes and the direct-goal test produced clear line of sight followed by
blocked line of sight as the probe moved around the pillar. The `los` field uses
`G_ClearLOS`; it is not a test of field of view, attention, reaction delay, or
stored enemy knowledge. These diagnostic traces do not update NPC memory.

This establishes locomotion and geometric occlusion, not effective flanking or
combat balance. Sound remains disabled, so no claim is made about audible barks.
Map scripts still run. Dedicated tests for goal replacement, direct entity
freeing, and loading or changing maps during a probe remain to be added.

This room now also hosts the lost-contact and shared-memory tests described in
`squad-ai.md`. Commander lost-contact tracking uses recorded positions; other
live-position uses remain listed there. Next, add temporary engage/flank roles,
with barks tied to actual plan transitions. Combat-point reservation cleanup
remains part of that later squad-plan test, not this locomotion probe.

## Inspection Tools

`python3 scripts/inspect-map.py t2_wedge` prints original map entity counts.
Add `--classes 'NPC_Stormtrooper|point_combat|waypoint'` for entity data. The tool
reads RBSP lump 0 from `assets0.pk3` through `assets3.pk3`, with later archives
taking priority. An archived `.ent` file overrides the entity lump. It does not
emulate mods or loose-file overrides and does not extract geometry.

The cheat command `nav dump` prints nodes and links within 1200 units of the
player. It reports cached flags without calling the state-changing edge validator.
It does not run an actor-specific path search, test current collision, or export
the complete map. A missing link can be outside the selected radius.
