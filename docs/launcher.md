# OpenJedvibe Launcher

## Requirements

You must own and install Jedi Academy to use this package. You must also own
and install Jedi Outcast to play the Jedi Outcast campaign.

The OpenJedvibe package does not contain game data. The launcher reads your game
data from the folders that you select. It does not change these source files.

## Start The Launcher

On Windows, use the **OpenJedvibe** Start Menu shortcut. On macOS, open
`OpenJedvibe.app`. On Linux, open `openjedvibe.desktop`. You can also start
`openjedvibe-launcher` directly.

Select the folder that contains `base/assets0.pk3`. You can select a game
folder, a `GameData` folder, or a `base` folder. Jedi Academy data is necessary
for both campaigns.

The launcher lists Jedi Outcast first. Its colours and controls match the
in-game selection menus. The launcher checks the files before each launch.

- Select **Continue** to load the most recent save in that campaign. This control
  is disabled if there is no save. Temporary and empty save files are ignored.
  The engine checks the save data when it loads the file.
- Select **New Game** to choose the difficulty. Jedi Academy then opens character
  creation. Jedi Outcast starts the campaign after difficulty selection.
- Select **Main Menu** to open the game menu without loading a save.
- Music starts automatically. Select **MUSIC: ON** to stop it. Select
  **MUSIC: OFF** to start it again. An audio-device failure does not stop the
  launcher. The window shows the error.

The music is a multi-instrument chiptune conversion of **Cantina Band**, by
John Williams. The MIDI source is Nonstop2k. The window shows this credit.
See [`launcher/music/CREDITS.md`](../launcher/music/CREDITS.md) for the source
URL, terms, and conversion command. Installed packages include `launcher/CREDITS.md`.
The source MIDI is not distributed. Builds use a checked-in score of about
112 KiB, not a WAV file. They do not need Python or the MIDI file for audio
conversion. The launcher generates the music in an SDL audio callback. File
access and score validation occur before playback. The callback does not
allocate memory or wait for UI updates.

Subtle blue bars in the background follow the music. They show voice energy in
16 pitch ranges, not a full frequency analysis. Muting the music makes the bars
fade out. The effect does not receive input or cover the campaign controls.

The synthesizer exposes the sample position, per-instrument energy, and the
pitch and age of the newest active voice on each channel. Visual data is one callback buffer behind synthesis to
approximate playback time. SDL does not expose the hardware playback cursor;
exact display-to-speaker timing still needs a device-specific check.

The launcher uses uppercase buttons and a larger initial window. At the normal
window size, all controls fit without a scrollbar. Smaller displays and long
error messages can still need scrolling. Windows uses a per-monitor DPI
manifest. macOS uses its high-resolution window support. Linux uses native
Wayland scaling or the X11 display DPI. Pointer input uses the drawable scale,
not the text scale.

New profiles use the desktop resolution, full-screen mode, and aspect-correct
field of view. Existing `OpenJK/openjk_sp.cfg` settings are not overridden.
Use the game settings menu to change the display settings.

For desktop development builds, run `scripts/play-sp.sh --launcher`. You can
add `--worktree NAME` before or after `--launcher`. The script updates the build
and opens the launcher with the configured game folders and worktree profile.
Select the campaign in the launcher. Do not add engine arguments to this command.

Use `openjedvibe-launcher --ui --profile PATH --ja-path PATH --jo-path PATH`
to open the window with explicit paths. The JO path is optional. For command-line
launches, use `--continue --campaign ja` or `--continue --campaign jo`.

## Pixel Band

Five musicians occupy the small bottom stage. They do not receive input.
From left to right, they play bass, supporting horn, lead horn, keys, and drums.
Each follows its own group of source channels. The lead follows clarinet and
alto reeds. The supporting horn follows brass and tenor reeds. The bass follows
synth bass and low winds. Keys follow piano and chords. Drums follow both
percussion parts.

Note starts can lift a horn by one source pixel, at most once per second.
Pitch changes select two finger positions. The winds hold their pose through
rests shorter than 300 milliseconds, then lower their instruments. Keyboard
presses and drum taps use small hand movements. Keyboard and drum props are
separate from the character sprites.

The head moves by one source pixel with the beat. This uses the
current arrangement's fixed 270 BPM tempo at half speed. A different song or
a variable tempo will need a separate beat map. Muting the music lowers the
horn, then selects a bored pose with occasional blinking. Playback resumes
through a ready pose. The original lead player keeps its approved muted poses.

Each pose is 32 by 32 pixels, including handheld objects. Each musician has eight poses. They use
original pixel patterns in `scripts/build-launcher-band.py`. The local reference
image is ignored by Git. The generator does not read it. Builds use the stored
`launcher/ui/cantina-player.tga` sheet and do not need Python. Run this command
to build the sheet again:

```sh
python3 scripts/build-launcher-band.py
```

The sprite uses nearest-neighbour filtering and whole framebuffer pixels.
At normal display scale, it is shown at twice its source size. Text keeps its
normal filtering. The stage uses document space and does not cover controls.

## Verification

The native import and launcher suite checks launch arguments, desktop defaults,
existing settings, latest-save selection, excluded save files, and campaign
separation. The desktop update suite checks update and profile behaviour.
The music suite uses synthetic MIDI data. It checks tempo changes, running
status, instrument selection, stereo pan, sustain, invalid data, and repeatable
output. It also checks the stored score without the source MIDI. Native tests
check score limits, simultaneous voices, stereo output, loop continuity, buffer
size independence, and instrument meters. A dummy-device test checks that
playback continues without UI updates and that mute clears the meters and
note state. Band tests check note starts, releases, overlapping voices, finger
positions, beat movement, rests, and the transition to and from mute. Sprite
tests check tile size, distinct poses, and repeatable sheet generation.

With `BuildTests=ON`, run CTest targets `launcher-music`, `launcher-synth`, and
`launcher-band-art`.
The project's full test configuration requires Boost. The native music tests
can also run without Boost on Linux:

```sh
c++ -std=c++17 -O2 -DSDL_MAIN_HANDLED -Ilauncher $(pkg-config --cflags sdl2) \
  tests/launcher_music.cpp launcher/music_synth.cpp launcher/music_player.cpp \
  $(pkg-config --libs sdl2) -o /tmp/launcher-music-tests
/tmp/launcher-music-tests launcher/music/cantina-band.score
python3 scripts/test-launcher-music.py
python3 scripts/test-launcher-band.py
```

A local optimized build generated 160 seconds of audio in approximately
0.31 seconds without audio output. This is a synthesis throughput check, not
an audio-latency measurement or a minimum-hardware guarantee.

Linux window checks use Xvfb and software OpenGL at 100%, 125%, 150%, and 200%
scale. All initial layouts fit without a scrollbar. Sprite pixels match the
source sheet at whole-pixel scales. The player changes from playing to bored
after mute. Music uses the SDL dummy audio device in these checks. The background
bars change during playback and clear after mute. Windows, macOS, Wayland,
hardware audio, and a full campaign load from Continue still need manual checks.

## User Data

The launcher stores user data in these locations:

- Windows: `Documents/My Games/OpenJK`
- macOS: `Library/Application Support/OpenJK` in your home folder
- Linux: `$XDG_DATA_HOME/openjk`, or `$HOME/.local/share/openjk`

The `bootstrap.ini` file stores the selected game folders and the last selected
campaign. Jedi Academy settings and saves use the profile root. Jedi Outcast
settings and saves use `campaigns/jo` below the profile root.

The launcher creates the Jedi Outcast import at
`campaigns/jo/OpenJK/zz_jo_campaign.pk3`. It creates this file from your local
game installations. This file is not part of the downloaded package.

The launcher shows setup and import errors in its window. It does not create a
persistent launcher log. Engine logs, when enabled, use the active campaign's
`OpenJK` profile directory.
