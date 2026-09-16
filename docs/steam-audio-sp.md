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
| `s_steamCache` | `1` | Read and write local acoustic caches |

Use `s_steam_status` to inspect the current scene, moving objects, active
sources, probes, filter values, and simulation times. `simulation_ms` reports
game-thread work. `reflection_ms` reports the most recent background simulation.

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

Useful listening cases are a closed door in Kejim, a mine shaft on Artus,
and a small passage connected to a large chamber on Yavin. Check both stationary
and moving listeners. Check speech intelligibility as well as environmental sound.

## Runtime and Build Details

The mixer processes 128-sample blocks. A background worker runs scene updates
and direct simulation, normally at 20 Hz. It runs reflections at 2 Hz, with
faster updates for changing sources. Steam Audio uses its Embree CPU backend
when available. Geometry and simulation inputs are held stable while the worker
runs. The audio mixer continues with the previous
completed parameters. Sound stop, restart, and level changes release the scene.

The initial packaged backend supports Linux x86-64 at output rates of at least
22050 Hz. Other configurations retain legacy audio. Configure with
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
python3 scripts/test-steam-audio-sp.py --campaign jo --bake
```

The standalone check measures transmission through a moving barrier, reflection
tails, paths around a partition, and probe serialization. The game check uses
a dummy audio device. It checks recorded PCM, cache reload, save/load, feature
disable, and sound restart. It does not replace listening checks.

References: [Steam Audio SDK](https://valvesoftware.github.io/steam-audio/doc/capi/index.html),
[integration guide](https://valvesoftware.github.io/steam-audio/doc/capi/integration.html),
and [SDK source and license](https://github.com/ValveSoftware/steam-audio).
