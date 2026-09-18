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

Headphone output is the default. Full and protected sources use HRTF for direct
sound and reflections. Full sources also use HRTF for available indirect paths.
Legacy sources do not use HRTF. The game still controls distance and channel gain.
Protected direct sound bypasses wall obstruction and transmission filters.

Set `s_steamHeadphones 0` for stereo speakers. This restores stereo panning and
the legacy direct mixer for protected sources. Set it to `1` for headphones.
The setting is saved. A change resets effect history and loads the existing map
cache; no new bake is needed. `s_steam_status` reports the output mode.
Listener-attached sounds stay centered. Head rotation updates each sound frame,
without a wait for the acoustic simulation. This does not add multichannel output.

Headphone checks:

- `tests/steam_audio_headphones.cpp` checks left/right, front/back, elevation,
  head rotation, gain, centered sources, convolution tails, and source reset.
- Run `tests/steam_audio.cpp` with an extra argument to test HRTF with walls,
  reflections, cached probes, and indirect paths. No argument tests speakers.
- Run `scripts/test-steam-audio-sp.py --headphones --flyby --burst --first-use`
  for headless game checks. `--routing` tests speaker routing, output-mode changes,
  protected headphone voice playback, and unchanged global sound energy.
- Run `tests/steam_audio_perf.cpp` with an extra argument for 32-voice HRTF cost.
  The local test used 2.30 ms per 256-frame block on average, with a 3.99 ms peak.
  The block budget at 44100 Hz is 5.80 ms. This is not a hardware guarantee.

Listen with headphones before further tuning. Check front/back and height cues,
voices behind walls, the Kejim lift, and sliding doors. Automated checks cannot
confirm that the default HRTF suits each listener.

## Controls

| Setting | Default | Function |
| --- | --- | --- |
| `s_steamAudio` | `1` | Enable Steam Audio; `0` restores legacy mixing |
| `s_steamHeadphones` | `1` | HRTF for full and protected sources; `0` selects stereo speakers |
| `s_steamReflections` | `1` | Enable reflections and room reverb |
| `s_steamPathing` | `1` | Use cached propagation paths when available |
| `s_steamReverb` | `0.2` | Set reflection and reverb gain, from 0 to 1 |
| `s_steamTransientReverb` | `1.0` | Reflection send multiplier for full-route one-shot world effects |
| `s_steamLimiter` | `1` | Limit combined mix peaks with about 3 ms of lookahead; `0` bypasses peak control |
| `s_steamTransmission` | `0.12` | Minimum blocked-path mid-band gain for full-route sources; `0` uses the raw material result |
| `s_steamRoute` | `-1` | Diagnostic override: automatic policy, or `0` legacy, `1` protected direct, `2` full; not archived |
| `s_steamCache` | `1` | Read and write local acoustic caches |
| `cg_spatialAmbience` | `1` | Submit environmental emitters across room visibility boundaries; `0` restores snapshot-only submission |
| `cg_alarmRelays` | `1` | Add Kejim Post perimeter-alarm relays at its control panel and gun base |
| `cg_boltFlyby` | `0` | Enable prototype close-pass cues with `1`; `2` also prints diagnostics |
| `cg_boltFlybyVolume` | `256` | Set the peak close-pass cue volume; playback clamps the value to 0 through 255 |

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

The limiter report gives the input peak, minimum gain, and number of reduced
frames since the last reset. Stereo channels share one gain envelope. Quiet
signals retain their gain. Dense effects can reduce gain smoothly to keep the
combined mix below full scale. Wet-only captures precede the limiter.

Use `s_steam_status sources` to list mixed sources, entity numbers, loop state,
channel gains, and sound names. `visible=0` means that the source entity is absent
from the current visual snapshot. A local sound set retains its entity number
even when another emitter uses the same sound asset.
Source details also include position, solid contents at that position, and the
smoothed occlusion and three transmission bands. Only full-route sources apply
these filters to direct audio. Each source also reports `route` and `rule`.
Scene-wide occlusion and transmission values remain the raw simulation minima.
See [sound routing](audio-routing.md) for the three routes and classification rules.

The transmission minimum limits blocked-path losses for full-route sources.
Low frequencies have twice this minimum gain; high frequencies have one quarter.
The material result is blended above the minimum, so material differences remain.
This is an audibility adjustment, not a physical wall measurement. Clear paths
retain their existing gain. Distance attenuation still applies. Global ambient
beds bypass spatial processing and do not occupy reflection slots.

The Kejim Post perimeter alarm has two additional audio emitters attached to
existing defense hardware. They use the original alarm's live on/off state.
They need no new game entities or save format. Only one visible control-panel
variant emits sound. The relays require `cg_spatialAmbience 1` and stop when the
original alarm stops. Set `cg_alarmRelays 0` for an original-source comparison.
The alarm now uses protected direct audio plus reflections. Acoustic obstruction
does not reduce its direct signal. The earlier alarm-specific transmission
multiplier was removed. Legacy distance attenuation and the mix limiter still apply.

The close-pass prototype uses quiet stock blaster-deflection clips. Use
`testflyby left` and `testflyby right` to audition them. Enable `cg_boltFlyby 1`
to test them in combat. A cue plays only when a foreign bolt passes within
72 game units of the listener after travelling at least 128 units. A solid wall
blocks the cue. Each trajectory can play once, with a shared 150 ms interval.
Player-owned shots, vehicle shots, and cinematics do not add these cues. Shots
with an existing flight sound retain that sound. Purpose-recorded pass-by clips
and desktop listening are still needed before enabling this prototype by default.
Ambient one-shots use the normal reverb send, including these cues.
The current peak setting reaches the playback limit of 255. Volume falls
linearly to zero at 72 units. Auditions use
the same curve at a distance of four units. The limiter remains active.

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
zero during steady Steam Audio playback. Legacy mixing can report overlaps
because it repaints its mix-ahead window. The capture now stores each legacy
frame only once. The overlap counter still reports those repaint operations.
`listener_motion` is the maximum distance from the first captured listener
position, in game units. `axis_motion` is the largest change in a listener basis
vector. Use these values to reject A/B captures made with a moving camera.

Use `s_steam_record 3 wet` to capture only reflections and indirect paths. This
excludes dry sound, music, and global ambient beds. Record the same blaster sound
inside and outside to compare the acoustic response. The capture is silent when
both reflections and pathing are disabled. The transient send multiplier applies
to one-shot effects, including blasters. Speech and loops keep their normal send.

Useful listening cases are a closed door in Kejim, a mine shaft on Artus,
and a small passage connected to a large chamber on Yavin. Check both stationary
and moving listeners. Check speech intelligibility as well as environmental sound.

### Audit Commands

Use these diagnostic commands with cheats enabled:

```text
s_steamAuditSound sound/ambience/prototype/alarm1
s_steamAuditEntity -1
s_musicvolume 0
s_steam_status sources
```

The sound filter requires the registered name without its file extension.
An empty name disables the sound filter. The entity filter uses a runtime entity
number; `-1` includes all entities. Both filters apply to legacy and Steam Audio.
Loop filtering occurs before legacy loop merging. An active audit filter also
mutes the raw music and video stream in both mixers. Stream timing continues.

A filter change clears the acoustic scene and its effect history. Local caches
can then reload. Isolation changes channel pressure and reflection priority;
a filtered capture is not a full-mix performance test.

Use `s_steam_probe x y z` to test an eye position against current world and mover
collision. It also tests a box from `{-15, -15, -48}` to `{15, 15, 8}` relative to
that position and traces down 256 units. This is a sampling check, not a test of
navigation reachability or all player stances. The source status report includes
the actual listener position and orientation, channel type, master volume, and
filter selection.

See [the campaign audio audit](audio-audit.md) for report commands and limits.

### Reflection Tuning

The current `s_steamTransientReverb` default is `1`. Compared with the previous
value of `2.5`, this reduces the full-route transient send by approximately 8 dB.
Protected sources, loops, and ambient one-shots use a normal send of 1. Lower `s_steamReverb` from `0.2` to `0.1` to reduce all reflection
and room-reverb output by approximately 6 dB. These changes do not shorten echoes.
Disable `s_steamPathing` when you compare reflection sends. Baked path output is
separate from the reflection gain. Keep `s_steamLimiter 1` for full-mix checks.
The transmission minimum changes blocked direct sound, not reflection decay.

Surface shape and materials also affect echoes. Large flat surfaces with little
scattering can produce distinct returns. Solid metal currently has absorption
`{0.20, 0.07, 0.06}` and scattering `0.1`. Concrete has absorption
`{0.05, 0.07, 0.08}` and scattering `0.2`. These low mid- and high-band absorption
values are investigation targets, not confirmed causes of harsh indoor sound.
Increasing absorption removes reflected energy. Increasing scattering distributes
more reflected energy into different directions; it does not absorb that energy.
The scene uses collision geometry, not all visible surface detail.

There is no separate early-reflection gain, late-reverb gain, or damping console
control in this version. The SDK supports three-band decay scaling for hybrid
reverb. This can shorten the late high-frequency decay, but it does not directly
remove a strong early echo. The current hybrid transition is 0.6 seconds, with
a crossfade into the parametric tail. Do not shorten this window as a general
indoor fix: the distant-cliff check requires a discrete return after 0.23 seconds.
The renderer limits estimated decay times to 0.1–6 seconds.

Material or reflection-model changes must change the cache version and rebuild
the affected scenes and probes. Send-level changes do not require a new bake.

The SDK definitions are in the [material reference](https://valvesoftware.github.io/steam-audio/doc/capi/scene.html),
[simulation reference](https://valvesoftware.github.io/steam-audio/doc/capi/simulation.html),
and [reflection effect reference](https://valvesoftware.github.io/steam-audio/doc/capi/reflections-effect.html).

## Runtime and Build Details

The mixer processes each 256-sample block once. It keeps the paint cursor at the
end of the previous block instead of repainting the mix-ahead window. The SDL
device lock covers DMA-buffer copies. DSP and capture file writes run outside
that lock. A background worker runs scene updates
and direct simulation, normally at 20 Hz. It runs reflections at 2 Hz while idle
and up to 10 Hz when sources change or the listener moves. Steam Audio uses its Embree CPU backend
when available. Geometry and simulation inputs are held stable while the worker
runs. The audio mixer continues with the previous
completed parameters. Sound stop, restart, and level changes release the scene.

Acoustic scene creation runs during level loading. The static BSP has its own
scene and acceleration structure. The main scene contains instances of the BSP
and brush models. Door movement updates this small instance hierarchy. It does
not rebuild the static BSP hierarchy. Hybrid convolution processes the first
0.6 seconds, so distant cliff returns remain discrete echoes. Parametric reverb
supplies the late tail. The larger blocks limit the added convolution cost.

New voices enter the mixer even when simulation is busy. They use the current
listener-room response until their source response is ready. Voice reset does
not wait for the worker. Results from the previous channel occupant are discarded.
The mixer accepts updated impulse responses during silence. This prevents an
unread SDK buffer from preserving the previous room's response for the next shot.
Reflection priority uses total channel gain with a small preference for current
sources. Head rotation does not select a different source merely because panning
changes. A 100 ms send crossfade connects source reflections and room reverb.
Existing tails continue through that crossfade. The SDK tail API stops convolution
work when its history is empty.

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
python3 scripts/test-steam-audio-sp.py --campaign jo --map kejim_post --acoustics --bake
python3 scripts/test-steam-audio-sp.py --burst --first-use
python3 scripts/test-steam-audio-sp.py --flyby --burst --first-use
python3 scripts/test-steam-audio-sp.py --ambient --bake
python3 scripts/test-steam-audio-sp.py --campaign jo --ambient --bake
python3 scripts/test-doors-sp.py --case ordinary --audio
```

The standalone check measures transmission through a moving barrier, reflection
tails, paths around a partition, probe serialization, and an echo from a cliff
40 metres away. It also checks voice reuse during a pending simulation.
The game check uses
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
the same flags as the standalone check above. Use those flags for
`tests/steam_audio_materials.cpp` to compare room size, scattering, and absorption
in a synthetic room. That check does not select material defaults for campaign
maps.

References: [Steam Audio SDK](https://valvesoftware.github.io/steam-audio/doc/capi/index.html),
[integration guide](https://valvesoftware.github.io/steam-audio/doc/capi/integration.html),
and [SDK source and license](https://github.com/ValveSoftware/steam-audio).
