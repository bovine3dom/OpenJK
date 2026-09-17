# Steam Audio for Single Player

The Linux x86-64 SP build includes Steam Audio 4.8.1. It processes world sounds
from both JA and JO. The existing SDL output device, sound assets, sound events,
music playback, and voice-completion timing remain in use.

## Features

- Direct sound uses partial occlusion, frequency-dependent material transmission,
  and air absorption. Filter changes blend over approximately 80 milliseconds.
- Collision brushes supply walls, floors, ceilings, and doors. Solid curved
  patches use a coarse mesh. Sky surfaces and player-only clipping volumes do
  not block sound.
- Existing material flags select absorption, scattering, and transmission
  presets. Examples include metal, glass, wood, rock, concrete, and carpet.
  Unspecified materials use a generic preset.
- Moving solid brush entities update the acoustic scene. A door can block sound
  while closed and let sound through when open. Removed barriers leave the scene.
- Up to four priority sources get source-dependent early reflections and reverb.
  Other world sounds feed a shared reverb at the listener's position.
- Steam Audio hybrid reverb combines early convolution with a parametric tail.
  The simulation covers 1.5 seconds. Rendered decay is limited to six seconds.
- Cached paths can carry sound around corners and through openings. Runtime
  visibility checks reject paths blocked by moving geometry and search for
  alternatives.
- Music, menu sounds, global voices, and announcer sounds bypass the effects.
  World WAV and MP3 sounds use the same processing path. Separate looping
  emitters keep separate acoustic paths.
- Environmental loops, local sound sets, and automatic speakers continue outside
  the visual snapshot. The sound pass uses the current server entity state and
  submits each emitter once. Walls, distance, and scripted on/off state still
  control the result. This does not add hidden models or NPCs to the render pass.

The direct mix retains the game's distance curves and stereo panning. Indirect
sound uses Steam Audio's stereo spatialization. This version does not enable
headphone HRTF processing for the direct mix.

## Controls

| Setting | Default | Function |
| --- | --- | --- |
| `s_steamAudio` | `1` | Enable Steam Audio; `0` restores legacy mixing |
| `s_steamReflections` | `1` | Enable reflections and room reverb |
| `s_steamPathing` | `1` | Use cached propagation paths when available |
| `s_steamReverb` | `0.2` | Set reflection and reverb gain, from 0 to 1 |
| `s_steamTransmission` | `0.12` | Minimum blocked-path mid-band gain; `0` uses the raw material result |
| `s_steamCache` | `1` | Read and write local acoustic caches |
| `cg_spatialAmbience` | `1` | Submit environmental emitters across room visibility boundaries; `0` restores snapshot-only submission |

Use `s_steam_status` to inspect the current scene, moving objects, active
sources, probes, filter values, and simulation times. `simulation_ms` reports
game-thread work. `reflection_ms` reports the most recent background simulation.

Use `s_steam_status reset` after the level has loaded to clear timing counters.
The next status report includes:

- `mix_peak_us`: the longest mixer update, including lock waits and capture writes.
- `lock_peak_us`: the longest time that the mixer held the SDL device lock.
- `callback_peak_us`: the longest interval between SDL callbacks. Pauses and
  operating-system scheduling can increase this interval.
- `underrun_frames`: frames by which the device cursor passed the last completed
  mix. This counter cannot detect multiple complete DMA-buffer wraps.
- `scene_peak_us`: the longest background scene commit since level initialization.
  This counter is retained by `s_steam_status reset`.
- `buffer_clears`: explicit buffer clears while Steam Audio is active. Runtime
  file access must not increase this counter. A sound stop can increase it.

These are mixer and callback measurements. They do not measure driver underruns.

Use `s_steam_status sources` to list mixed sources, entity numbers, loop state,
channel gains, and sound names. `visible=0` means that the source entity is absent
from the current visual snapshot. A local sound set retains its entity number
even when another emitter uses the same sound asset.
Source details also include position, solid contents at that position, and the
smoothed occlusion and three transmission bands used by the mixer. Scene-wide
occlusion and transmission values remain the raw simulation minima.

The transmission minimum keeps authored gameplay cues audible through BSP walls.
Low frequencies have twice this minimum gain; high frequencies have one quarter.
This is an audibility adjustment, not a physical wall measurement. Clear paths
retain their existing gain. Distance attenuation still applies. Global ambient
beds bypass spatial processing and do not occupy reflection slots.

`cg_spatialAmbience` also works with legacy mixing. Steam Audio supplies wall
transmission and indirect paths when enabled. Unpositioned global ambient sets
still follow the map's ambient-set selection and crossfade. They do not define
a fixed source in another room.

## Local Acoustic Caches

The first use of a level builds its acoustic scene from the installed BSP.
The engine stores the static scene under `cache/steamaudio` in the current
campaign profile. Cache names include the BSP checksum, SDK version, and cache
format version. The loader checks the data length and checksum before use.

To add precomputed propagation paths and room responses, load a level and run:

```text
s_steam_bake
```

This command pauses the game while it works. It generates up to 256 probes,
bakes paths and room responses, and writes the result to the same local cache.
Later visits load this data automatically. The bake treats moving barriers as
open. Live path validation applies their current state during play.

Before a bake, direct processing and live reflections work normally. Explicit
probe-based pathing becomes available after the bake. Sparse probes can miss
small rooms; the shared reverb then uses live simulation until the listener
moves to another location.

No generated scene, probe, or sound files are distributed with the project.
To regenerate a cache, remove its files from the campaign profile and reload
the level. To compare without cache data, set `s_steamCache 0` and reload.

## Listening and Diagnostics

Use these commands to compare the final mix:

```text
s_steam_record 3
s_steam_emit sound/weapons/blaster/fire.wav
```

The first command records three seconds to a WAV file under `captures` in the
current profile. The second emits a world sound at the listener. Supply
`x y z` after the sound name to place it elsewhere. Repeat with `s_steamAudio 0`
for a legacy reference. Captures contain local game audio and stay in the profile.
Capture output also reports overlapping and missing frames. Both counts must be
zero during steady Steam Audio playback. Legacy captures can contain overlaps
because the legacy mixer repaints its mix-ahead window.

Useful listening cases are a closed door in Kejim, a mine shaft on Artus,
and a small passage connected to a large chamber on Yavin. Check both stationary
and moving listeners. Check speech intelligibility as well as environmental sound.

## Runtime and Build Details

The mixer processes each 128-sample block once. It keeps the paint cursor at the
end of the previous block instead of repainting the mix-ahead window. The SDL
device lock covers DMA-buffer copies. DSP and capture file writes run outside
that lock. A background worker runs scene updates
and direct simulation, normally at 20 Hz. It runs reflections at 2 Hz, with
faster updates for changing sources. Steam Audio uses its Embree CPU backend
when available. Geometry and simulation inputs are held stable while the worker
runs. The audio mixer continues with the previous
completed parameters. Sound stop, restart, and level changes release the scene.

Acoustic scene creation runs during level loading. The static BSP has its own
scene and acceleration structure. The main scene contains instances of the BSP
and brush models. Door movement updates this small instance hierarchy. It does
not rebuild the static BSP hierarchy. Hybrid convolution processes the first
0.15 seconds; parametric reverb supplies the late tail.

Runtime file access preserves queued Steam Audio output. The legacy filesystem
buffer clear erased the mix-ahead window on first asset access. This caused an
audio gap without a mixer underrun. Explicit sound stops still clear the buffer.

The initial packaged backend supports Linux x86-64 at 44100 Hz. The SDK also
supports 48000 Hz, but the current SDL rate selector does not select that rate.
The default HRTF does not initialize at 22050 Hz. Lower output rates use legacy
audio. Use `s_khz 44` and `snd_restart` to select Steam Audio. Configure with
`-DBuildSteamAudio=OFF` to omit the SDK. The default supported build downloads
the official SDK archive with a pinned SHA-256 hash and installs `libphonon.so`
beside the engine. License files are under `licenses/steamaudio`.

This is a geometric acoustic approximation. Material presets do not determine
exact wall thickness or structural resonance. Model props, deforming actors,
sub-BSP additions, and underwater propagation need further work. The original
32-channel sound-effect limit still applies.

## Checks

```sh
c++ -std=c++11 -pthread -I shared -I build/sp/cache/steamaudio-src/include \
  tests/steam_audio.cpp shared/sound/steam_audio.cpp \
  -L build/sp/cache/steamaudio-src/lib/linux-x64 \
  -Wl,-rpath,"$PWD/build/sp/cache/steamaudio-src/lib/linux-x64" -lphonon \
  -o build/steam-audio-test
build/steam-audio-test
python3 scripts/test-steam-audio-sp.py --bake
python3 scripts/test-steam-audio-sp.py --campaign jo --bake --device-samples 256
python3 scripts/test-steam-audio-sp.py --rate 22
python3 scripts/test-steam-audio-sp.py --campaign jo --map kejim_post --first-use --bake
python3 scripts/test-steam-audio-sp.py --campaign jo --map kejim_post --alarm --bake
python3 scripts/test-steam-audio-sp.py --ambient --bake
python3 scripts/test-steam-audio-sp.py --campaign jo --ambient --bake
python3 scripts/test-doors-sp.py --case ordinary --audio
```

The standalone check measures transmission through a moving barrier, reflection
tails, paths around a partition, and probe serialization. The game check uses
a dummy audio device by default. It checks capture continuity, separate effect
modes, recorded PCM, cache reload, save/load, feature disable and enable, and
sound restart. Results include timing counters and clipped-sample counts. Use
`--audio-driver pulseaudio` on a desktop with that SDL driver for a device test.
The game window remains hidden. WAV captures still come from the internal mixer;
they are not device-loopback recordings. These tests do not replace listening
checks. Compare the internal capture with a desktop loopback recording if
crackling continues.

The ambient checks use stock console and generator emitters outside the visual
snapshot. They check that each loop is submitted once, stops with snapshot-only
submission, and returns when spatial ambience is enabled. The 32-source and
large-mesh performance check is `tests/steam_audio_perf.cpp`; compile it with
the same flags as the standalone check above.

References: [Steam Audio SDK](https://valvesoftware.github.io/steam-audio/doc/capi/index.html),
[integration guide](https://valvesoftware.github.io/steam-audio/doc/capi/integration.html),
and [SDK source and license](https://github.com/ValveSoftware/steam-audio).
