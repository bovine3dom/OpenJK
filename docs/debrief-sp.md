# Mission Debrief Regression

## Fault and Correction

The mission-complete screen uses animated Ghoul2 portraits for its speakers.
After a map with a sky portal, Rend2 applied a sky-only batch rule to the
portrait and to later UI drawing. The portrait, briefing text, Continue button,
and cursor were missing.

The batch rule now applies only to world rendering. It excludes both 2D drawing
and `RDF_NOWORLDMODEL` scenes. Portrait viewport coordinates are converted from
the UI's top-left origin even when a map remains loaded. Uniform-buffer flushes
also use offsets relative to the mapped range, so later scenes can upload their
constants correctly.

`ui_report` now prints the focused menu, menu item name, and virtual
cursor coordinates. This helps distinguish a drawing failure from an input or
menu-state failure.

## Text and Portrait Proportions

Mission-complete headings, statistics, dialogue, and buttons use IBM Plex Sans
SemiBold. Text keeps its original colors and row heights. Proportional text
measurements control wrapping and button hit areas.

Speaker portraits keep their vertical field of view. The horizontal field of
view uses the pixel viewport's aspect ratio. This keeps faces in proportion
on both 4:3 and widescreen displays.

## Automated Test

Build a package, then run:

```sh
python3 scripts/test-debrief-sp.py --package build/ready
python3 scripts/test-debrief-sp.py --package build/ready --renderer rdsp-rend2 --msaa 4 --width 1280 --height 720
```

Python 3, Xvfb, xdotool, FFmpeg, and the original game assets are required.
The test creates a private X server and isolated profiles. It uses software
OpenGL and dummy audio. It does not change original profiles, saves, or assets.

The fixture loads `t1_sour` and activates its real `end_level` target. This
executes the game's mission-end code and opens the debrief. It bypasses the
mission objectives; it is not a campaign walkthrough.

The default test covers vanilla with MSAA off, then Rend2 with MSAA off and 4x.
It captures Luke's portrait, injects mouse input to click Continue, captures
Kyle's portrait, then clicks Okay. The UI report must confirm arrival at
`ingameMissionSelect1`. Image checks require speaker pixels and briefing text.
The test saves two Luke frames for motion inspection.

To check portrait proportions across display sizes, use the first run's
`results.json` file as a reference:

```sh
python3 scripts/test-debrief-sp.py --package build/ready --width 1280 --height 720 --reference build/smoke/debrief.00yu3fuo/results.json
```

The image check compares the horizontal and vertical spread of skin pixels.
It permits small changes from speaker animation. All three renderer settings
passed at 960 x 720 and 1280 x 720 in `debrief.00yu3fuo` and
`debrief.zbntbldp`.

Mouse coordinates in the UI are relative virtual coordinates, not display
pixels. The test resets the virtual cursor and waits for engine frames after
injected motion. It checks focus before clicking rather than invoking menu
actions directly.

Results and captures are stored under `build/smoke/debrief.*`. The three default
cases passed in `build/smoke/debrief.ngrh_uao`. Before the fix, the lower text,
portrait, and Continue button were absent from the Rend2 captures.

A 1280 x 720 Rend2 run with 4x MSAA also passed the mouse-driven sequence in
`build/smoke/debrief.kfsblutb`. A separate P630 hardware capture confirmed the
portrait and text with 4x MSAA in `build/smoke/debrief.jopa0kbw`.

## Desktop Verification

Use the checklist in `human_todo.md`. Confirm speaker animation, voice playback,
lip synchronization, pointer visibility, and progression through Continue and
Okay after a completed mission. Dummy audio cannot establish sound quality or
synchronization on a desktop audio device.
