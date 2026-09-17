# Audio Handoff

## Next Work: Campaign Audibility Audit

**Stop point:** the user requested a handoff before implementation. This work has
not started beyond inspection of the existing code and audit tools.

### Latest User Requirements

- Build an emitter inventory and audibility report for **both JO and JA**.
- Establish where each sound is clearly audible with legacy audio. Steam Audio
  must keep it highly audible in at least those places. Source presence alone is
  not a pass condition.
- The user suspects that the current `kejim_post` alarm still fails this test,
  even with the extra emitters. Earlier gain and relay tests do not establish
  adequate audible coverage.
- Indoor echoes sound too harsh. Investigate surface shape, material absorption,
  scattering, reflection timing, and send levels. Explain the available controls.
- The fly-by prototype is promising but too quiet. Increase its useful level and
  check it during combat, with the limiter active.

### Audit Tasks

- [ ] Inventory speakers, local sound sets, loops, global ambient beds, and
  scripted sound references in every campaign map. Record activation and stop
  targets, positions, brush-model bounds, sound assets, radii, and channel types.
- [ ] Check asset resolution against the files that the runtime uses, including
  the JO import overlay. Report missing assets and unresolved references.
- [ ] Find candidate listener positions near emitters, activation controls,
  navigable connections, and both sides of doors. Reject solid positions. Record
  unsampled areas and sampling limits; do not label sparse coverage as complete.
- [ ] Establish the legacy audible footprint at those positions. Account for
  snapshot membership, distance curves, local-set volume, and activation state.
  Test scripted-off and scripted-on states separately.
- [ ] Compare matching legacy and Steam Audio runs. Hold the camera, source event,
  master volume, and other mix settings fixed. Record the visibility, relay,
  limiter, reflection, pathing, and bake settings explicitly.
- [ ] Measure source-isolated or otherwise controlled PCM, including RMS level,
  peak level, onset, and tail energy. Compare gain in dB against the legacy result.
  Use configurable thresholds for a strong legacy signal and an acceptable loss.
  A proposed starting limit is 3 dB of loss at strong legacy listening positions;
  validate that limit before treating it as an acceptance rule.
- [ ] Run the report across both campaigns. List results by campaign, map, emitter,
  listener position, and tested state. Include reproduction commands, settings,
  source provenance, skipped cases, and local A/B capture paths. Rank large losses,
  abrupt changes over short distances, duplicates, clipping, and stale sounds.
- [ ] Use `kejim_post` as a focused regression: test the perimeter control panel,
  gun emplacement, nearby exterior routes, and relevant interior positions. Verify
  the original alarm and relays together, including activation, stop, and save/load.
- [ ] Fix high-priority losses with appropriate source placement, coverage, or
  cue-specific treatment. Preserve useful wall filtering and check mix headroom.
- [ ] Keep generated game audio and extracted acoustic data local under `build`.
  Automated measurements identify candidates; desktop listening must judge balance
  and whether a cue belongs in a particular area.

### Reflection and Fly-by Follow-up

- [ ] Explain and test existing controls: `s_steamReverb` (0.2 overall wet gain),
  `s_steamTransientReverb` (2.5 one-shot send multiplier), `s_steamTransmission`
  (0.12 minimum blocked-path mid-band gain), `s_steamReflections`, `s_steamPathing`,
  and `s_steamLimiter`.
- [ ] Check the material presets in `code/client/snd_steam.cpp`. Solid metal uses
  absorption `{0.20, 0.07, 0.06}` and scattering `0.1`. Concrete uses
  `{0.05, 0.07, 0.08}` and scattering `0.2`. Flat surfaces with low absorption and
  low scattering can produce strong, distinct echoes; this is an investigation
  target, not a confirmed cause of the user's report.
- [ ] Evaluate useful tuning controls for early reflections versus late reverb,
  high-frequency damping, scattering, and decay. The current early window is
  hard-coded to 0.6 seconds in `shared/sound/steam_audio.cpp`. Material changes must
  invalidate or rebuild affected scene and probe caches.
- [ ] Raise and expose the fly-by cue level. Current maximum pass volume is 96/255,
  with linear falloff to zero at 72 game units; audition volume is 72/255. These
  values are hard-coded in `code/cgame/cg_ents.cpp`. Keep ownership checks, wall
  checks, the once-per-trajectory rule, and the 150 ms shared interval.

Useful starting points: `scripts/test-steam-audio-sp.py` supplies headless launches,
captures, and focused checks. `scripts/audit-jo.py` parses ICARUS blocks, and
`scripts/import-jo.py` indexes retail archives. `scripts/inspect-map.py` currently
assumes JA RBSP files and `assets0.pk3` through `assets3.pk3`; extend or replace
those assumptions for a two-campaign audit. `s_steam_status sources` gives source
positions, channel gains, snapshot membership, and smoothed acoustic parameters.

## Current Status

An optional close-pass prototype is available with `cg_boltFlyby 1`. It uses
quiet stock deflection clips, geometric closest-approach checks, wall checks,
and rate limits. `testflyby left|right` provides an audition. The default remains
off while the cue character is evaluated. Ambient one-shots no longer receive
the stronger gunshot reverb send.

The perimeter alarm now has relays at its existing control panel and gun base.
All three sources follow the original alarm state. The relay check verifies
separate emitters, clear sound at the panel, save/load, and scripted stop.
The combined alarm, dense-fire, first-use, and bake run passed in
`build/smoke/steam-audio.2q6avow8`.

The user confirms that the first-use gap is gone. A dense-fire check then found
159 full-scale samples with four simultaneous blaster shots. A stereo-linked
lookahead limiter now controls the combined mix. The same capture passed without
full-scale samples in `build/smoke/steam-audio.z2uez6_f`.

Reflection selection now uses pan-independent priority and hysteresis. Source
and room sends crossfade over 100 ms. The SDK tail API retires empty convolution
history. In the rotating 32-source test, mean DSP time fell from 3.12 ms to
2.03 ms per 5.80 ms block after the tail change.

The room-entry stutter is resolved in the user's report. Acoustic rendering now
uses 256-sample blocks and a 0.6-second early-reflection window. The previous
0.15-second window could not preserve distant canyon echoes. A synthetic cliff
40 metres away produces its first return after approximately 0.23 seconds.
One-shot world effects use a stronger send (`s_steamTransientReverb`, default 2.5).
Speech and loops retain their normal send level. New voices enter processing
while the simulation worker is busy; they use the listener-room response until
their own response is ready. Reused voices reject the previous source's result.
The mixer also accepts room impulse responses during silence. The SDK can then
publish the current room response before the next shot. A regression check moves
a silent listener away from a cliff and rejects an echo from the old position.
`s_steam_record 3 wet` isolates indirect output for indoor/outdoor comparisons.

The latest Kejim Post checks cover file access, alarm gain and stop, indoor/outdoor
wet captures, a silent effects-off control, baking, save/load, and sound restart.
They passed in `build/smoke/steam-audio.k7lo_ul9`. The JA first-use and ambient
checks passed in `build/smoke/steam-audio.qpzvrks1`. These use SDL's dummy device;
the latest balance still needs desktop listening.

The Kejim perimeter alarm was active, but its blocked-path transmission fell to
about 0.00001 in the mid band. Its source was outside solid geometry. The mixer
now uses a tunable transmission minimum (`s_steamTransmission`, default 0.12).
Low frequencies pass more strongly than high frequencies. Clear-path and distance
gains remain unchanged. Listener-attached global ambient beds now bypass spatial
effects and no longer occupy reflection slots.

The remaining first-use gap had a separate cause: filesystem operations cleared
the queued DMA audio. Steam Audio retained its advanced paint cursor, so the
erased window stayed silent. This did not count as a mixer underrun. Runtime file
access now preserves that output. Explicit sound stops still clear the buffer.
The Kejim Post file-access regression failed before the fix with one buffer clear
and passed after it with zero. The passing baked run is
`build/smoke/steam-audio.v3pmryij`; the failing run is `steam-audio.3e66jzlw`.

The user confirms that almost all crackling is gone. Acoustic scene creation runs
during level loading. `tests/steam_audio_perf.cpp` checks 32 sources, repeated
reflection updates, and a moving door against a large static mesh. With the new
0.6-second window, mean DSP time was 1.01 ms per 5.80 ms block on this host.

The static BSP now has a separate acceleration structure. Moving doors update
the parent instance hierarchy. In the large-mesh test, mean door-update time
fell from about 41 ms to 1.05 ms. `s_steam_status` reports `scene_peak_us` for
background scene commits. Door transmission and cached-world tests remain part
of the regression checks.

Environmental loops, local sound sets, and automatic speakers now have a sound
pass independent of the visual snapshot. It uses live entity state in stable
entity order. `cg_spatialAmbience 0` selects the old snapshot-only behavior.
Local ambient loops retain their entity number and no longer inherit channel
metadata from an earlier loop. Zero-volume local sets do not allocate channels.

JA and JO ambient checks passed with baked probes. The checks kept a stock JA
console (`sound/ambience/cp_17_lp`) and a JO generator
(`sound/ambience/kejim/kejim_generator`) active outside the visual snapshot.
Each emitter appeared once, stopped with snapshot-only submission, and returned
when spatial ambience was enabled. Steam Audio captures had no overlapping or
missing frames. Results are in `build/smoke/steam-audio.1atpgq8r` and
`build/smoke/steam-audio.4_lhfnbu`. The audible result still needs desktop review.

### Earlier Timing Fixes
The user reported severe crackling. The old mixer moved `s_paintedtime` backwards
on each update. Steam Audio then processed overlapping samples with advanced
filter and reverb state. A two-second JA capture contained 71,552 overlapping
frames. The new capture continuity check failed on that build.

The Steam Audio paint cursor now advances without overlap. The SDL device lock
now covers the DMA copies, not DSP or capture file writes. Status output includes
peak mixer time, peak device-lock time, callback intervals, and mixer underruns.
Check `build/ready` and the package source manifest before a comparison. Another
workstream can publish a newer build.

For an immediate legacy-audio comparison, use:

```text
s_steamAudio 0
```

This is a temporary diagnostic setting. The selected long-term implementation
is Steam Audio. The user wants to retain it unless it cannot be made to work.

## First Tasks

- [x] Reproduce the paint-cursor defect in an internal capture. Add a regression
  check that rejects overlapping or missing frames while Steam Audio is active.
- [x] Keep the Steam Audio paint cursor continuous. Release the device lock during
  DSP and capture file writes.
- [x] Add separate direct, reflection, and baked-path capture checks. Check disable,
  enable, cache reload, save/load, and sound restart.
- [x] Obtain desktop feedback: the severe crackling and room-entry stalls are fixed.
- [x] Reproduce and fix filesystem buffer clears during active DSP playback.
- [x] Check the Kejim alarm's blocked-path gain and scripted stop.
- [x] Check a delayed cliff echo and isolate indirect output in WAV captures.
- [ ] Check the latest first-use, alarm, and acoustic-contrast changes on the
  desktop. Record the output device, rate, settings, and bake state.
- [ ] Listen to Steam Audio enabled and disabled at the same location. Compare
  direct processing, reflections, and pathing separately.
- [ ] Determine whether the corruption is present in an internal WAV capture,
  in device-loopback audio, or in both. Internal captures bypass the SDL device
  consumer and cannot prove that device playback is correct.
- [ ] Measure peak mixer duration and device underruns. Check short peaks, not
  only average CPU use. Correlate the peaks with crackles, scene updates, source
  changes, and reflection-result changes.
- [ ] Check sample continuity at every 256-sample boundary, loop boundary, channel
  reuse, and transition between legacy and Steam Audio processing.

## Debugging Hypotheses

The following items separate confirmed defects from remaining investigation:

1. **Mixer and device timing.** Paint-cursor overlap was confirmed and fixed.
   `S_PaintChannels` uses complete 256-sample blocks. It rounds the requested write
   window down to a complete block. Check device callback timing and underruns on
   the desktop. A callback interval is not a measurement of a driver underrun.
2. **Channel identity and DSP resets.** Legacy loop channels are cleared and
   allocated again each frame. Steam Audio keeps filter and convolution state
   by channel slot. Check resets, slot reassignment, and transitions between
   eligible world sounds and excluded local sounds.
3. **Processing cost and synchronization.** Scene updates and simulations run
   on a worker. DSP still runs during painting of the audio buffer. Check time
   spent in the mixer, SDL audio locking, worker completion, and `ResetVoice`.
4. **Gain and clipping.** Check conversions between signed 16-bit PCM, normalized
   floats, legacy channel gains, and the integer paint buffer. Measure clipping
   and non-finite samples before final conversion. Check direct, path, and reverb
   contributions separately.
5. **Result ownership and transitions.** Check the lifetime and synchronization
   of reflection impulse responses. Path coefficients are copied into owned
   arrays after worker completion. Verify that no other mutable SDK output is
   read while the worker writes it.
6. **Short sounds and tails.** Check zero-filled blocks, MP3 stream boundaries,
   reflection priority changes, and tail handling when a sound ends or a slot
   is reused. A non-silent output test does not detect these discontinuities.

## Implementation Map

| File | Responsibility |
| --- | --- |
| `code/client/snd_steam.cpp` | Game integration, collision-brush and patch extraction, material presets, moving brush entities, channel slots, caches, diagnostics, and WAV capture |
| `code/client/snd_steam.h` | Mixer hooks and stubs for builds without Steam Audio |
| `shared/sound/steam_audio.cpp` | SDK objects, effects, asynchronous simulation, reflections, pathing, and probe baking |
| `shared/sound/steam_audio.h` | Processing interface; 256-sample blocks and 32 voice slots |
| `code/client/snd_mix.cpp` | WAV/MP3 interception, block scheduling, and final mix integration |
| `code/client/snd_dma.cpp` | Initialization, shutdown, listener updates, and separate looping emitters |
| `shared/sdl/sdl_sound.cpp` | Existing SDL device output and DMA buffer handling |
| `cmake/Modules/SteamAudio.cmake` | Pinned SDK download, linking, and installation |
| `docs/steam-audio-sp.md` | Controls, architecture, limits, and test commands |

The current backend uses the official Steam Audio 4.8.1 SDK on Linux x86-64.
It uses Embree for CPU acoustic queries when available. `libphonon.so` is
installed beside the engine, with an `$ORIGIN` runtime search path.

World sounds receive material-dependent transmission, partial occlusion, and
air absorption. Up to four priority sources receive source-dependent reflections.
Other sources feed a listener-room reverb. The direct mix retains legacy distance
curves and stereo panning. Direct headphone HRTF processing is not enabled.

Music, menu sounds, global voices, and announcer sounds bypass the effects.
Ordinary world WAV and MP3 channels use the processing hooks. Check these
classification rules during channel-reuse investigation.

## Controls and Captures

| Control | Current default or use |
| --- | --- |
| `s_steamAudio` | `1`; set to `0` for legacy mixing |
| `s_steamReflections` | `1`; set to `0` to disable reflections and room reverb |
| `s_steamPathing` | `1`; uses baked paths when available |
| `s_steamReverb` | `0.2`; reflection and reverb gain |
| `s_steamTransientReverb` | `2.5`; one-shot effect send multiplier |
| `s_steamTransmission` | `0.12`; minimum blocked-path mid-band gain |
| `s_steamCache` | `1`; reload the level after changing this for a cache comparison |
| `s_steam_status` | Scene, source, probe, filter, and timing information |
| `s_steam_status reset` | Clear mixer and device timing counters before a comparison |
| `s_steam_bake` | Bake paths and room responses for the current level |
| `s_steam_record 3` | Record three seconds of the final paint-buffer mix to a local WAV |
| `s_steam_record 3 wet` | Record only indirect output |
| `s_steam_emit sound/weapons/blaster/fire.wav` | Emit a test world sound at the listener; optional `x y z` arguments set its position |

Captures are under `captures` in the current campaign profile. Acoustic caches
are under `cache/steamaudio`. The current cache format version is 3. Cache names
include the BSP checksum and SDK version. Keep generated game audio and acoustic
data local.

Capture output reports `overlap_frames` and `gap_frames`. Both must be zero in
a steady Steam Audio capture. Legacy mixing can report overlaps because it
repaints the mix-ahead window. The SDK's default HRTF does not initialize at
22050 Hz. This rate now selects legacy audio before SDK initialization. Use
`s_khz 44` and `snd_restart` for Steam Audio.

Baking is explicit and pauses the game. It creates up to 256 probes with moving
barriers treated as open. Runtime path validation applies current barriers.
Direct processing and live reflections work before a bake.

## What Was Verified

- The standalone test measured lower signal energy through a barrier, a reflection
  tail, and an indirect route around a partition. Sealing the route suppressed
  that indirect output.
- Static-mesh and probe serialization passed round-trip checks.
- JA and JO headless checks used a dummy audio device. They checked non-silent
  PCM captures, capture length, cache reload, save/load, disable, and sound restart.
- The two default renderer smoke tests passed.

**These checks did not establish clean audible playback.** They did not measure
sample discontinuities, device underruns, or crackle. The user's playback report
takes precedence over those earlier pass results.

The timing-fix checks passed in JA `t1_sour` and JO `kejim_base` at 44100 Hz,
with baked probes. All Steam Audio captures had zero overlapping or missing
frames. The JO check also used a 256-frame device buffer. These checks used SDL's
dummy device. This session has no PCM output device and cannot connect to the
desktop audio server. Internal captures do not establish clean device playback.

Final artifacts:

| Check | Result directory under `build/smoke` |
| --- | --- |
| Regression before the fix | `steam-audio.qigruw98` |
| JA, 44100 Hz, baked probes | `steam-audio.lx9s5753` |
| JO, 44100 Hz, 256-frame device buffer, baked probes | `steam-audio.ox3dynuq` |
| JA, 22050 Hz legacy fallback and restart | `steam-audio.q6mit6rw` |

The separate effect checks reported no mixer underruns and no clipped samples.
Peak device-lock times were 1–16 microseconds. Peak reflection-mode mixer times
were 19.4 ms in JA and 15.1 ms in JO. These are headless test measurements,
not desktop performance limits. JA reported an underrun during the blocking
probe bake, before the steady capture checks. Many-source stress testing and
source-reuse waveform checks remain open.

The SDK supplies synchronization for the opaque reflection impulse-response
handle. The integration reads simulation outputs after worker completion and
copies path coefficients. See the
[SDK maintainer's explanation](https://github.com/ValveSoftware/steam-audio/issues/256#issuecomment-1522219723).

Relevant artifacts from implementation:

- `build/smoke/steam-audio.aymjfz17`: latest JA check before publication.
- `build/smoke/steam-audio.cluirfeo`: JO check with asynchronous simulation and baking.
- `tests/steam_audio.cpp`: synthetic SDK processing checks.
- `scripts/test-steam-audio-sp.py`: headless game check and internal captures.

## SDK Issues Already Resolved

- Path-effect creation required an initialized HRTF object even though rendering
  uses speaker output. Supplying no HRTF caused a crash.
- Baking uses a non-null progress callback. A null callback caused a crash.
- Loaded probe batches need `iplProbeBatchCommit` before use.
- Whole-scene serialization/loading was unsuitable for the Embree path. The
  integration stores a default-backend static mesh and loads it into an existing
  Embree scene. Do not replace this with whole-scene serialization without testing.
- Repeated scene commits were expensive. The integration tracks geometry changes
  and performs required commits on the simulation worker.

## Later Work

After clean playback is established:

- [ ] Tune transmission and reflection levels with listening comparisons in Kejim,
  Artus Mine, and Yavin.
- [ ] Check memory use and DSP cost with many active and looping sources. Reflection
  effects and SDK source state currently exist for all 32 voice slots.
- [ ] Review sparse-probe coverage and the live-reverb fallback for missed rooms.
- [ ] Extend geometry coverage for model props, deforming actors, and sub-BSP additions.
- [ ] Evaluate underwater propagation and optional headphone HRTF processing.
- [ ] Add other supported build platforms after the Linux implementation is stable.
