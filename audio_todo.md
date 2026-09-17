# Audio Handoff

## Current Status

The user confirms that almost all crackling is gone. Brief sound gaps on first
use and frame-time peaks near doors remain. Work now targets those stalls and
environmental emitters that stop outside the visible room.

First-use changes move acoustic scene creation into level loading. Hybrid
convolution now allocates and processes 0.15 seconds, which matches its transition
to parametric reverb. The 1.5-second simulation and late reverb remain in use.
An immediate sound start no longer resets its DSP again when the mixer assigns
its sample time. `tests/steam_audio_perf.cpp` checks 32 sources, repeated reflection
updates, and a moving door against a large static mesh. The initial comparison
reduced mean DSP time from 1.83 ms to 0.40 ms per 128-sample block on this host.

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

**A mixer timing defect is fixed. Clean device playback still needs a check.**
The user reported severe crackling. The old mixer moved `s_paintedtime` backwards
on each update. Steam Audio then processed overlapping samples with advanced
filter and reverb state. A two-second JA capture contained 71,552 overlapping
frames. The new capture continuity check failed on that build.

The Steam Audio paint cursor now advances without overlap. The SDL device lock
now covers the DMA copies, not DSP or capture file writes. Status output includes
peak mixer time, peak device-lock time, callback intervals, and mixer underruns.
These changes need a listening check on the user's device before effect tuning.

The timing-fix package is:

```text
build/packages/20260916T230737092770910-adf45fc6
```

Check `build/ready` and the package source manifest before a comparison. Another
workstream can publish a newer build. The package includes uncommitted audio fixes.

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
- [ ] Reproduce the crackling on the user's real audio output device. Record the
  campaign, level, output rate, device, settings, and whether probes were baked.
- [ ] Listen to Steam Audio enabled and disabled at the same location. Compare
  direct processing, reflections, and pathing separately.
- [ ] Determine whether the corruption is present in an internal WAV capture,
  in device-loopback audio, or in both. Internal captures bypass the SDL device
  consumer and cannot prove that device playback is correct.
- [ ] Measure peak mixer duration and device underruns. Check short peaks, not
  only average CPU use. Correlate the peaks with crackles, scene updates, source
  changes, and reflection-result changes.
- [ ] Check sample continuity at every 128-sample boundary, loop boundary, channel
  reuse, and transition between legacy and Steam Audio processing.

## Debugging Hypotheses

The following items separate confirmed defects from remaining investigation:

1. **Mixer and device timing.** Paint-cursor overlap was confirmed and fixed.
   `S_PaintChannels` uses complete 128-sample blocks. It rounds the requested write
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
| `shared/sound/steam_audio.h` | Processing interface; 128-sample blocks and 32 voice slots |
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
| `s_steamCache` | `1`; reload the level after changing this for a cache comparison |
| `s_steam_status` | Scene, source, probe, filter, and timing information |
| `s_steam_status reset` | Clear mixer and device timing counters before a comparison |
| `s_steam_bake` | Bake paths and room responses for the current level |
| `s_steam_record 3` | Record three seconds of the final paint-buffer mix to a local WAV |
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
