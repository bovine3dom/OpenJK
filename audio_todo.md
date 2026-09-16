# Audio Handoff

## Current Status

**Blocker: the user reports that the Steam Audio output is extremely crackly.**
Treat the current integration as an experimental implementation, not a finished
audio upgrade. The cause of the crackling is not known. Fix this before work on
additional effects or acoustic tuning.

The implementation is committed in `3195d7b7` (`Start sketching out Steam Audio`).
The last package published during implementation was:

```text
build/packages/20260916T181523493202169-145525b6
```

That package contains the then-uncommitted audio implementation. Its revision
suffix refers to the earlier HEAD. Check `build/ready` and the package source
manifest before a comparison; another workstream can publish a newer build.

For an immediate legacy-audio comparison, use:

```text
s_steamAudio 0
```

This is a temporary diagnostic setting. The selected long-term implementation
is Steam Audio. The user wants to retain it unless it cannot be made to work.

## First Tasks

- [ ] Reproduce the crackling on the user's real audio output device. Record the
  campaign, level, output rate, device, settings, and whether probes were baked.
- [ ] Compare Steam Audio enabled and disabled at the same location. Also compare
  direct processing alone with reflections and pathing enabled separately.
- [ ] Determine whether the corruption is present in an internal WAV capture,
  in device-loopback audio, or in both. Internal captures bypass the SDL device
  consumer and cannot prove that device playback is correct.
- [ ] Measure peak mixer duration and device underruns. Check short peaks, not
  only average CPU use. Correlate the peaks with crackles, scene updates, source
  changes, and reflection-result changes.
- [ ] Check sample continuity at every 128-sample boundary, loop boundary, channel
  reuse, and transition between legacy and Steam Audio processing.
- [ ] Add a focused regression check for the identified cause. Validate actual
  playback before increasing effect levels or adding features.

## Debugging Hypotheses

The following are investigation targets, not confirmed causes:

1. **Mixer and device timing.** `S_PaintChannels` now uses complete 128-sample
   blocks while Steam Audio is active. It rounds the requested write window down
   to a complete block. Check this against `s_paintedtime`, the DMA ring, device
   callback sizes, channel start times, and the existing `s_mixahead` setting.
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
| `s_steam_bake` | Bake paths and room responses for the current level |
| `s_steam_record 3` | Record three seconds of the final paint-buffer mix to a local WAV |
| `s_steam_emit sound/weapons/blaster/fire.wav` | Emit a test world sound at the listener; optional `x y z` arguments set its position |

Captures are under `captures` in the current campaign profile. Acoustic caches
are under `cache/steamaudio`. The current cache format version is 3. Cache names
include the BSP checksum and SDK version. Keep generated game audio and acoustic
data local.

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
