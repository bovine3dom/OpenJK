# Campaign Audio Audit

## Scope

The static scan covers the 34 JA and 26 JO maps in
`scripts/atmosphere-review.json`. This list includes the JO bonus map `pit`.
The scan reads RBSP and IBSP files, external entity files, sound sets, and
literal ICARUS script dependencies. It records source files and map hashes.

The runtime scan makes sparse, source-isolated PCM comparisons. It does not
establish complete campaign coverage or good desktop playback. No source-presence
check is an audibility pass. The 3 dB loss threshold is provisional.

Keep reports, extracted assets, WAV files, and acoustic caches under `build`.
Do not distribute these generated game files.

## Static Scan

Run from the repository root:

```sh
python3 scripts/import-jo.py GameData GameData_JO build/audio-audit/jo-profile
python3 scripts/audit-audio.py --campaign ja --game-dir GameData/base \
  --game-dir build/ready/OpenJK --output build/audio-audit/ja.json
python3 scripts/audit-audio.py --campaign jo --game-dir GameData/base \
  --game-dir build/ready/OpenJK --game-dir build/audio-audit/jo-profile/OpenJK \
  --output build/audio-audit/jo.json
```

Supply game directories in low-to-high search priority. Include the actual JO
import overlay, not just the JO retail archives. Within a directory, later PK3
names take priority over earlier names. Archives take priority over loose files
unless `--loose-first` selects `fs_dirbeforepak 1`. Loose paths retain their case,
as required on Linux. Check the runtime `path` output against these directories.

Use `--map NAME` for selected maps or `--all-maps` for all visible BSP files.
The JSON report includes:

- Raw entity fields, speaker flags, target links, positions, and brush bounds.
- Global ambient-set controls, local sets, loop assets, subwaves, and radii.
- Brush-model sound stages: start, middle loop, and stop.
- Sound-set timing and volume ranges. The volume range applies to subwaves;
  it is not the loop master volume.
- Reachable literal script references, sound channels, and control operations.
- Missing literal sound files, unresolved sets and scripts, and dynamic names.

Missing references can belong to unused branches or removed entities. They are
review candidates, not proof of an audible defect. Dynamic expressions, NPC
custom sounds, generated sounds, and script activation order need runtime tests.
The sound resolver assumes English and tries WAV before MP3, as the runtime does.
Loose script aliases need a runtime file check. The runtime requests an uppercase
`.IBI` extension. Ambiguous script paths are marked for review.

## PCM Comparisons

Build the engine before you run the measurements. The engine must include the
source-isolation and listener-probe commands.

```sh
python3 scripts/measure-audio.py --inventory build/audio-audit/jo.json \
  --alarm --listeners 11 --seconds 10 --matched
python3 scripts/measure-audio.py --inventory build/audio-audit/ja.json --freeze-scripts
python3 scripts/measure-audio.py --inventory build/audio-audit/jo.json --freeze-scripts
python3 scripts/measure-audio.py --inventory build/audio-audit/jo.json \
  --acoustic-tuning
```

Each run writes `build/audio-audit/measure.*/results.json`. It also retains console
logs and local capture paths. Reports record the immutable package path, source
manifest hash, inventory hash, mix settings, source states, listener coordinates,
limiter data, and bake state. The runtime JO overlay is removed after the run;
the importer can rebuild it from the recorded package and retail files.

The default scan tests at most one loop-speaker or global-bed case and two valid
listener positions per map. Use `--emitters` and `--listeners` to increase these limits.
Candidates include offsets near speakers, radius samples, nearby activation
controls, navigation anchors, and both sides of nearby doors. Collision checks reject solid points and occupied test
hulls. Door candidates use static bounds and can be invalid after a door moves.
These checks do not establish navigation reachability. Rejected and unsampled
positions remain in the report. Use `--entity` to select a static entity index
and `--listener` to select a listener label for a repeat test.

The sampler disables camera smoothing and requests a firearm with ammunition
to avoid the forced third-person saber view. It corrects the teleport offset until the actual listener
reaches the requested position. New captures record maximum listener motion and
axis changes during recording. Position or orientation changes invalidate an A/B
sample. Older packages without these measurements report unverified motion.

Use `--freeze-scripts` to hold ICARUS task updates in the loaded mission state.
The sampler first skips the game camera and waits for acoustic mixing to resume
after any active ROQ movie. It then freezes scripts and disables cinematic
cameras. It does not stop physics or moving brush entities. Freezing before
startup scripts run can leave ambient beds silent; such samples do not establish
an audible footprint. The underlying cheat command is `ICARUS freeze 1`; use
`ICARUS freeze 0` to resume task updates. Do not use a frozen mission state as
proof of normal script activation behavior. An active mission-selection screen
stops the scan with an error. Complete that loadout through normal input before
a mission-state test.

The modes are:

| Mode | Steam Audio | Spatial ambience | Alarm relays |
| --- | --- | --- | --- |
| `legacy` | Off | Off | Off |
| `steam` | On | On | On |
| `steam-matched`, optional | On | Off | Off |

Other mix settings stay fixed. Reflections, pathing, and the limiter are enabled.
A new profile has no baked probes unless you supply `--bake`. The script records
RMS, peak, clipping, first signal, final-window energy, and capture continuity.
It also records a 100 ms RMS envelope and sample jumps at 256-frame boundaries.
A large jump can come from the source asset; it is not proof of mixer corruption.
For steady loops, first signal and final-window energy are not event onset or
reverb-decay measurements. Longer captures reduce loop-phase bias.

A sound-name filter can include several emitters. It changes channel pressure
and reflection priority. Full-mix tests must follow isolated comparisons.
The game stays in its loaded mission state. The scan does not replay every script
branch. Toggled speakers get an additional stopped-state capture after their
tails have time to decay. This is not a complete state-coverage test.

The default strong-signal threshold is `--strong-dbfs -35`. The default candidate
loss threshold is `--loss-db 3`. A silent legacy capture has no finite gain ratio.
Invalid captures and weak legacy samples are not acceptance results. Desktop
listening must judge balance, useful coverage, and whether a cue belongs in an area.

## Measured Changes

The wider scan found a geometry-loader defect in `bespin_undercity`. Four retail
brushes have 166 to 170 sides, including bevel planes. The audio loader rejected
brushes above 128 sides and fell back to legacy audio. The loader now uses the
actual side-lump bounds instead of that fixed limit. Other bounds checks remain.
The corrected scene has 166,242 triangles. The map smoke test passed in
`build/smoke/steam-audio.w7owb6gr`; two valid A/B samples are in
`build/audio-audit/measure.qb45ly0o/results.json`. One sample still has a large
blocked-path loss. Successful initialization is not an audibility pass.

The sampler checks the active backend before each capture. A requested Steam
mode that falls back to legacy is an error, not an A/B comparison.

The measurements below predate [the routing policy](audio-routing.md).
That policy replaces the Kejim alarm-specific transmission floor with protected
direct audio plus reflections. The earlier floor was twice `s_steamTransmission`,
limited to 1, for the looping alarm asset in `kejim_post` only.

An initial 10-second comparison found about 3.4 dB of loss at two exterior
positions with a strong legacy signal. After the change, a baked 11-position
comparison found gains of -0.09 dB at the exterior route and +0.02 dB at the
upper western door. All 11 samples had matching positions and orientations,
no capture motion, and continuous PCM. The stopped alarm capture was silent.
The local report is `build/audio-audit/measure.b77vb146/results.json`.

The panel gain was +10.08 dB with the relays, and the panel-approach gain was
+7.58 dB. These increases need a desktop balance check. At seven other positions,
legacy was silent. Do not assign a finite gain ratio to those samples. With the
relays disabled, the original source alone remained about 8.7 dB below legacy
at the two exterior positions. The relay contribution remains necessary in this
fixture. These tests do not establish every mission state or route.

Wet-only blaster experiments found a much stronger response in the Kejim room
than in the canyon. Setting `s_steamTransientReverb` to 1 reduced room wet RMS
by about 8 dB in that pilot. Setting `s_steamReverb` to 0.1 also reduced wet level.
The reflections-off control was silent. The report is
`build/audio-audit/measure.eg2y615n/results.json`. This pilot used earlier camera
controls, so use it as tuning evidence, not as a final spatial acceptance test.

The synthetic room test compares room size, metal scattering, and absorption.
Higher high-frequency absorption reduced reflected energy by about 6.8 dB in
that test. Higher scattering did not reduce total reflected energy. Scattering
is not a substitute for absorption. No material or reverb defaults changed.
See [Steam Audio controls](steam-audio-sp.md) for the available settings and
cache requirements.

## Sparse Campaign Results

These runs use one selected case and up to two valid listener positions per map.
They are not exhaustive emitter or route tests.

| Run under `build/audio-audit` | Result |
| --- | --- |
| `measure.0p2t_yta` | All 34 JA maps considered; 50 valid samples in 25 maps; 40 strong legacy samples; one loss candidate. |
| `measure.yf2vlhnm` | All 26 JO maps considered; 46 valid samples in 23 maps; 31 strong legacy samples; three loss candidates. |
| `measure.qb45ly0o` | Corrected Bespin Undercity loader; two valid strong samples; one loss candidate. |
| `measure.5fb2un81` | Yavin Temple after movie readiness checks; two valid strong samples; no loss candidate. |
| `measure.slmx6v77` | JA `t1_fatal`, entity 372; six valid strong samples; three loss candidates. |
| `measure.5rhxc60a` | Focused JO speaker offsets; 30 valid samples; eight loss candidates. |

The JA sweep did not establish an audible footprint for `academy1` through
`academy6` or `t1_inter`. A separate check found an active mission-selection
screen and a paused server in `academy2`. Early script freezing allowed camera
movement, but all 14 follow-up samples were below the strong-signal threshold.
Do not substitute those samples for normal mission-state tests. `t2_trip` and
`t3_rift` had no supported static loop-speaker or global-bed case.

The JO sweep rejected 47 Bespin Undercity comparisons because the backend was
inactive, and one other comparison because the listener moved during capture.
The later loader check replaces the invalid Bespin results. `pit` had no supported
case. The original Yavin Temple run reached a client-command overflow during a
movie. The sampler now waits for acoustic mixing before it sends player commands.
The Yavin Temple retry passed, with gains of -0.33 and -0.48 dB. With the Bespin
and Yavin replacements, JO has 50 valid samples in 25 maps, 35 strong legacy
samples, and four loss candidates.

Losses remain review candidates:

- JA `taspir2`, entity 48, `offset-1-1`: -4.65 dB in the sparse sweep.
- JA `t1_fatal`, entity 372: losses from -3.14 to -9.27 dB in focused offsets.
  The tested navigation anchor gained 1.78 dB.
- JO `bespin_streets`, entity 32, `offset-0--1`: -3.17 dB.
- JO `bespin_undercity`, entity 1, `offset-0--1`: -16.83 dB after the loader fix.
- JO `cairn_dock1`, entity 4: about -11.5 dB at blocked-side offsets. Two nearby
  patrol anchors matched legacy within 0.1 dB in `measure.7xtsgyjn`.
- JO `cairn_dock1`, entity 25: about -14.7 dB at three tested offsets. Two of
  these positions had no floor within the probe range.
- JO `cairn_reactor`, entity 2: -5.98 dB above the source. The two tested
  navigation anchors were silent in both modes in `measure.3kriyt51`.
- JO `doom_shields`, entity 25: about -12.5 dB at two positions with no nearby
  floor. The two navigation samples in `measure.sd4w8yj2` had no strong-signal
  loss candidate.

No clipping, duplicate-loop, or stale-sound warning occurred in these completed
reports. A collision-free point is not proof of a usable gameplay route.
Review the captures and mission state before a source-placement or gain change.

## Tests

```sh
python3 scripts/test-audit-audio.py
python3 scripts/test-measure-audio.py
python3 scripts/test-steam-audio-sp.py --audit-freeze --flyby --burst --first-use
python3 scripts/test-steam-audio-sp.py --campaign jo --map kejim_post --alarm --bake
```

The fly-by check compares volume settings 96 and 192. It also captures the louder
cue with four blaster shots and checks clipping with the limiter active. This
checks mixed headroom, not whether the cue can be heard during desktop combat.
See [Steam Audio controls](steam-audio-sp.md) for reflection tuning and device
playback limits.
