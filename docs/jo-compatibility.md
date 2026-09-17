# JO Compatibility Checks

## Static Report

Run this command from the repository root:

```bash
python3 scripts/audit-jo.py
```

The default output is `build/jo-audit.json`. Use `--academy`, `--outcast`, and
`--output` to change the input directories and the output file.

The report checks 26 single-player maps and 1,356 compiled scripts in the local
retail assets. It records these items:

- Entity handlers, empty handlers, and spawn-flag branches that return early.
- Changed spawn-flag labels for flags that the map uses.
- NPC classes and model references.
- Script dependencies and animation references.
- Sound and runtime model references. Compiled `misc_model` references are excluded.
- Weapon, Force, inventory, and objective commands, with script byte offsets.
- Map transitions, item entities, and navigation-file availability.
- Script-property registration and operation-specific setter/getter dispatch.
- Empty or unimplemented handler candidates and reviewed behavior differences.

Each finding includes a source reference. Map entity numbers refer to the BSP
entity list, not runtime entity numbers. Script byte offsets refer to the original
ICARUS file. The report also lists each map's literal script dependencies.

Treat the report as an inspection aid. A file can exist but use an incompatible
format or an incorrect animation set. Variable script references, script branches,
rendered materials, and normal mission completion need runtime checks. A changed
spawn-flag label is not proof of a behavior change.

Some references are also absent from the original JO assets or handler tables.
For example, `cairn_assembly` contains an `item_shield` entity that has no item
definition in either campaign. Do not treat every finding as a new integration
defect.

`behaviors` contains both referenced operations and the original JO interface
catalog. Each entry records registration, dispatch, source location, and script
references. `used_behavior_status_counts` counts only referenced operations.
`implemented` means that a source handler exists; it does not prove equivalent
behavior. `verified` entries have a specific regression check.

Reviews are stored in `scripts/jo-behavior-reviews.json`. Missing operations and
handler-review candidates remain visible. The current report includes the
unused `SET_FULLNAME` setter/getter gap and unused handler stubs. Two retail
`SET_FACE_MOVEDIR` references have no registered handler in either source tree.
These findings require separate review; they are not all new port defects.

## Prisoner Heads

JO has separate `head` and `head_off` surfaces. JA removes the `_off` suffix when
it loads a model. This gives both surfaces the same name and prevents the second
prisoner variant from selecting its head.

The local importer changes the alternate head names to `_alt`. It changes the
model, skin, and NPC surface references together. Surface indices and geometry
stay the same. Save loading restores the alternate surfaces for living
`Prisoner2` actors from earlier imports.

The shared surface-status query now checks explicit overrides for surfaces that
are off by default. An ancestor's no-descendants flag still takes priority.

Both prisoner variants pass the surface and save/load checks with vanilla and
Rend2. A Rend2 capture also shows the complete back of the second variant's head.

```bash
python3 scripts/test-jo-sp.py --prisoners --renderer rdsp-vanilla
python3 scripts/test-jo-sp.py --prisoners --renderer rdsp-rend2
```

## Nar Shaddaa Bouncer Surfaces

The Rodian without a vest uses `torso_augment_off` for its back panel. JA's `_off`
name handling prevented the NPC definition from enabling this surface. The importer
now uses `torso_augment_alt` in the mesh, skin, and NPC definition. It also keeps
the two Rodian fin surfaces distinct. Dismemberment cap names retain their existing names.
Loading an older save enables the back panel on a living `Rodian2` actor.

Run `python3 scripts/test-jo-sp.py --bouncers` to check the two native bar bouncers.
The check activates their retail spawner, checks surface flags, captures both actors,
and checks save/load. It passes in vanilla and Rend2.

## Lando Ship Boarding

JO waypoints use the character origin. JA route checks used bounds measured from
the floor. The extra height made the ship stair route in `ns_starpad` appear
blocked by its ceiling. Lando stopped inside the entrance while his script
waited for him to reach the cockpit.

JO route checks now use bounds relative to the character origin. The navigation
cache version has changed so existing routes are calculated again. The retail
boarding scripts complete and show the roof and fuel objectives. This also
passes with a save made while Lando was stuck.

```bash
python3 scripts/test-jo-cinematics.py --case boarding --renderer rdsp-vanilla
python3 scripts/test-jo-cinematics.py --case boarding --renderer rdsp-rend2
```

The test starts Lando at the ship ramp. It retains the retail geometry, routes,
collision, and scripts. It checks his console animation, speech, and both new
objectives. Use `--save PATH` to check recovery from a saved boarding sequence.

## Artus Topside Handoff

Desann uses scripted noclip movement to reach his dialogue position. JA's newer
navigation path did not set a movement speed for this mode. He stayed still, and
the script continued to wait for his arrival after Tavion's first line.

The shared movement code now sets the NPC's walk or run speed for noclip movement.
The scene completes without skipping in vanilla and Rend2. The Rend2 check also
loads a save from before the scene. A save from inside the reported hang has not
been verified; the normal save command rejects the test's attempt to save during
the cinematic.

The importer now supplies NPC saber definitions and JO's class/rank Force defaults.
JO supplied these defaults in code; JA reads them from NPC data. Existing explicit
power levels and saber colors take priority. The combat check now requires damage,
the defeat scene, active aftermath sabers, and the transition to Valley.
The idle test player is returned to the arena after large knockback. The test does
not reduce health or apply damage directly.

## Progression and Boss Checks

These focused checks pass:

```bash
python3 scripts/test-jo-sp.py --progression
python3 scripts/test-jo-sp.py --puzzle
python3 scripts/test-jo-sp.py --galak
python3 scripts/test-jo-sp.py --world
python3 scripts/test-jo-cinematics.py --case topside
```

The progression check moves the player to retail holocrons and the saber pickup.
It checks a push button before and after the push unlock, a pull step, jump height,
and active speed. The button checks hold the camera still with noclip. They use
the normal Force commands and the retail scripts, not direct button activation.
The check then verifies the Force wheel, save/load, and the retail exit target.
It does not establish normal traversal of the complete training route.

The separate puzzle check pulls all four fountains with the normal Force command.
It checks water, bridge, and grate heights. It then checks their response to Kyle's
position and crosses to the far ledge with Speed and Jump. The check uses retail
holocrons. It moves Kyle to the test room and back to its entrance between attempts.
The timed crossing allows three jump points to account for software-rendering input delay.
No direct mover activation is used. Focused runs have passed in both renderers.
The full publication run still has intermittent crossing failures. Mover heights and
load response pass, but the timed crossing is not yet a reliable regression check.
The test uses a smaller window and four software-rendering threads during the timed crossing.
It does not establish reliable movement at very low frame rates.

JO's saber pickup has no saber-definition name. The campaign loader supplies the
shared `player` definition. The pickup then fires its target, completes the
objective, and runs the original level-one saber-skill grants after Kyle's line.
Save loading also supplies this default to older item entities.

The Galak check uses the retail boss spawner. It checks real missile firing, then
uses diagnostic damage to check shield and generator behavior. It does not
establish aiming accuracy, weapon balance, or full normal-play boss completion.
The checks pass in both renderers, including saves with a depleted shield and a
destroyed generator. Galak's death fires the retail completion script. The debris
sequence exposed a shared collision-code null-client access, which is now fixed.

Both game targets use the same Galak controller source. The shared game adapts
its surface flags, surface names, API calls, and six animation names. The importer
reserves `BOTH_CIN_45` through `BOTH_CIN_50` for these animations.

The collision check samples the thin opaque water boundary in `yavin_swamp`.
The boundary retains opacity but has no solid contents in JO mode. JA keeps its
existing collision rules.

Rend2's fog constant block previously held only 16 volumes. Yavin Swamp has 20,
which caused a stack overwrite during loading. CPU and shader arrays now use
the map-format limit of 256. The loader rejects counts above that limit.
The Yavin Swamp check now passes with Rend2 and is included in both renderer runs.

The spawn-flag report entry for `valley` entity 2 is now implemented and tested.
JO uses bit 2 to hide completion statistics; JA uses it for optional story audio.
The JO loading panel and script request are restored. See `jo-statistics.md`.
The earlier assessment that this difference was non-blocking did not establish
presentation parity.

Use `campaign_status all` to include hidden objectives in a status report.
Use `surface_status <targetname> <surface> [...]` to inspect model surface indices
and flags. With cheats enabled, use `galak_test <targetname>` to inspect the boss.
The optional arguments `damage [generator]` apply a diagnostic hit. Use
`mover_status <targetname>` to inspect a training mover's position and angles.

Keep manual results in `human_todo.md` and implementation tasks in `todo.md`.
