# JO Cinematic Animation Compatibility

## Audit

Run the complete reference check from the repository root:

```bash
python3 scripts/audit-jo-cinematics.py --check
```

The report is `build/jo-cinematic-audit.json`. It checks all 1,356 retail scripts,
including scripts outside the `cinematics` directory. It records 869 animation
commands, 247 distinct values, and nine hand-prop attachments. One animation
value is the legacy `-1` no-change instruction.

Each record identifies the script, byte offset, actor, map, available model
animation sets, and imported animation name. The check also compares the complete
JO engine name table with the shared engine table. It reports missing names,
missing frame data, unresolved actors, and absent props.

The current static check has no unresolved animation names, no missing frame
data, and no animation-set gaps for the identified actors. It reports two unused
stormtrooper references in `cinematic24`. Those names occur only in that script;
the retail maps and spawn scripts do not create those actors.

## Complete Sets

An animation name alone is not sufficient. JO and JA can use the same name for
different poses, frame ranges, or timings. A JO cinematic actor must use the JO
skeleton and its complete animation file.

The shared engine now includes all 314 JO-only animation names in an append-only
table. Existing JA indices remain the same. The importer creates cinematic NPC
definitions for every JO humanoid model and copies all of its skin variants.
These definitions use the original 72-bone JO skeleton and complete JO animation
data. Existing cinematic-slot aliases remain available for older imports and
the Galak controller.

The game selects this data for NPCs with the cinematic spawn flag or a
`cinematic` target-name prefix. The prefix also covers the Luke and Desann duel
actors in `cinematic26`, which do not have the cinematic spawn flag. NPC identity,
weapons, and behavior remain separate from the selected animation data. Ordinary
gameplay actors retain the shared JA animation set.

Two retail script names have no supplied clips. The importer changes
`BOTH_SCARED1` to the available crouch pose `BOTH_CROUCH3`, and
`BOTH_DEADFORWARD1` to `BOTH_DEAD1`. These are explicit replacements for invalid
retail references, not restored missing assets.

## Verified Scenes

- Kejim office: Jan sits in the chair. Kyle uses the inspection poses with the
  crystal attached. The official receives the crystal. The conversation completes.
- Artus Topside: Tavion and Jan use the paired restraint poses. Desann enters,
  speaks, and attacks Kyle with Force powers. Damage starts the defeat scene.
  Desann and Tavion have active saber blades and attached hilts. The scene reaches Valley.
- Kejim CCTV: the officer moves on the floor. His walk animation advances at its
  authored rate. The test checks bone frames, not only the animation name.
- Valley shrine and Yavin Trial ending: actors reach their movement goals.
  Both scenes complete and return player control.
- Nar Shaddaa bar: the bartender uses the idle, gesture, and cower animations.
  The scene completes and returns player control.
- Doomgiver reunion: the paired hug and release animations play. The rescue
  objective completes, and the gameplay Jan uses the shared JA animation set.

The existing CCTV and Artus opening checks remain part of the test suite.
Run all scene checks with:

```bash
python3 scripts/test-jo-cinematics.py --package build/ready
```

Use `--case office`, `--case topside`, `--case bar`, or `--case rescue` for one
scene. Add `--renderer rdsp-rend2` to select Rend2. The test uses retail scripts
and diagnostic activation of their start targets. The reunion check opens the
retail cell door, which starts its cinematic. The test does not skip the selected
scene. It can skip an earlier map-entry scene to reach the selected test.
The Artus combat check returns the idle player to the arena after large knockback.
It preserves health and enemy state. Desann must cause the damage that starts the
defeat scene. Normal player movement and pursuit around obstacles need a separate playthrough.

Logs and captures are in `build/jo-cinematics/`. With
`d_cinematicAnimations 1`, each script animation request records its actor,
animation set, first frame, frame count, and availability. The scene checks reject
unavailable animations on the staged actors. `cinematic_status <targetname>` also
reports the animation set and attached model paths.

The shared navigation graph does not cover every JO scene goal. JO cinematic
actors and actors with script-locked enemies can use a clear direct path.
Collision checks still apply. This also lets Desann approach Kyle after knockback.

JA's foot-slide correction could start a held walk animation at near-zero speed.
The actor then moved without a visible gait. JO cinematic and noclip actors now
use the authored animation rate. Kyle's aiming and startled poses have separate
captures in the Artus test. The test checks that his rendered body faces Desann.

Static coverage does not establish every camera angle or every full mission
sequence. Keep normal-play visual checks in `human_todo.md`.

## Saves

New saves use format 4 because the animation tables are larger. The reader keeps
support for this project's formats 1 through 3. It reads the old table length,
initializes the added slots, and reloads JO animation data where required.

Loading an older save also replaces the old models of living staged actors with
their cinematic models. It preserves their identity, state, surface overrides,
weapon and prop attachments, and animation timers. It rebuilds the bone bindings.
The source save is not rewritten. The older office and Artus pre-scene saves pass
with Rend2. A separate JA test passes the format-3 to format-4 round trip, checks
stored state, and rejects versions 0 and 5.

For a complete replay, use a save from before the scene. A missing pose that an
older build already skipped is not replayed during an in-progress save load.
Save loading repairs missing NPC saber definitions after all entity slots are restored.
Saber initialization can create an entity, so it must not run during slot restoration.
The old Artus Desann actor also receives his missing Push default and Force resources.
The repair preserves the script-controlled Pull, Grip, and Lightning levels.
Artus saves from before this fix pass the combat and aftermath checks. This includes
format 3 in both renderers and format 4 in vanilla. The JA format-3 migration and
format-4 save round trip also pass with Rend2.
See `save-migration.md` for the format policy.
