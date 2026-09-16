# Jolt Performance Check

## Method

Date: 16 September 2026. Renderer: Rend2. GPU: Intel HD Graphics P630.
Resolution: 1280 by 720. The test uses hardware OpenGL through the offscreen
SDL driver. It does not use software rendering.

The test places stock stormtroopers in the same area of `t1_sour`.
The camera position stays fixed. NPC AI is frozen. Live actors use ten rigs
at most. Corpse batches must sleep before the next batch starts.
The test checks the actor count before and after measurement.

Final samples use a three-second warmup and a ten-second measurement.
Each final scene has two runs, except the disabled ten-corpse baseline.
That baseline has one eight-second sample. Initial samples also use eight
seconds. Driver cache state and system load are not controlled.

## Results

The table gives median approximate frame rates. Higher values are better.
The active-rig row uses the disabled ten-NPC scene as its reference.

| Scene | Reactions disabled | Reactions enabled |
| --- | ---: | ---: |
| Ten standing NPCs | 57.1 FPS | 54.8 FPS |
| Ten active physical rigs | 57.1 FPS | 51.7 FPS |
| Ten sleeping corpses | 56.8 FPS | 60.7 FPS |
| Thirty sleeping corpses | 35.7 FPS | 36.6 FPS |
| Sixty sleeping corpses | 23.0 FPS | 21.5 FPS |

Native and physical corpses have different poses. These differences affect
rendering work. The enabled sixty-corpse runs ranged from 16.8 to 26.3 FPS.
Use these results as a local comparison, not a fixed performance guarantee.

The initial sixty-corpse run gave 15.4 FPS. Its average game-module work was
20.5 milliseconds per rendered frame. After the changes, that work was
3.4 to 6.6 milliseconds per rendered frame. This metric includes the whole
game module. It is not a measurement of the Jolt solver alone.

## Changes

- Skip physics updates when a dead body's world has no active bodies.
- Keep the published bone pose and bounds of a sleeping corpse.
- Skip unchanged brush transforms when the brush velocity is zero.
- Wake dead bodies when a collision mesh is enabled or disabled.

Moving platforms still update. Impacts still wake bodies. Tests check platform
movement, removal of support, corpse save/load, and the saber floor attack.
Sleeping worlds keep their pose-history clock current.

## Repeat the Check

```bash
python3 scripts/benchmark-sp.py --jolt-scene idle --characters 10 --runs 2 --seconds 10 --warmup 3 --cvar g_joltReactions 0
python3 scripts/benchmark-sp.py --jolt-scene idle --characters 10 --runs 2 --seconds 10 --warmup 3
python3 scripts/benchmark-sp.py --jolt-scene active --characters 10 --runs 2 --seconds 10 --warmup 3
python3 scripts/benchmark-sp.py --jolt-scene corpses --characters 60 --runs 2 --seconds 10 --warmup 3
```

Use corpse counts of 10, 30, and 60. Add `--cvar g_joltReactions 0` for the
native corpse comparison. Results, settings, GPU identity, and screenshots
are stored under `build/benchmark-sp`.

The live test can hold a selected rig with `jolt_balance 60`. The argument
sets the hold time in seconds, from zero to sixty. It is a cheat command.
