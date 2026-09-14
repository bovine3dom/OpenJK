# SP SSAO Tests

## Run

Build a package before running this test. The test does not change the package
or the original assets. The package must
contain the current renderer and an exact copy of `scripts/rend2-ssao.cfg`
at `PACKAGE/OpenJK/rend2-ssao.cfg`. The build script copies `scripts/rend2-*.cfg`
automatically. The test stops before game startup if the fixture is absent or
different.

From the repository root, run:

```sh
python3 scripts/test-ssao-sp.py --package build/ready
OJK_ASSETS=/path/to/GameData python3 scripts/test-ssao-sp.py --package build/ready
```

Python 3, FFmpeg, Xvfb, and the smoke script dependencies are required.
The test uses `smoke-sp.sh`: Xvfb, software OpenGL, and 640 x 480 captures.
It does not use hardware offscreen rendering. MSAA 0 and 4 run in sequence,
with separate processes and fresh profiles. The log must confirm each requested
MSAA value. Use a driver that supports 4x MSAA for this comparison.

## Controls

| Control | Values and effect |
| --- | --- |
| `r_ssao` | `0` disables SSAO; `1` enables SSAO. Use `vid_restart` after a change. |
| `r_ext_multisample` | `0` disables MSAA; `4` requests four samples. Use `vid_restart` after a change. |
| `r_ssaoAmbientOnly` | Default `1`: apply SSAO to ambient light and IBL. `0`: apply SSAO once to all per-pixel Lightall lighting. No restart is required. |
| `r_ssaoDebug` | Default `0`: scene. `1`: raw AO from `quarterFbo[0]`. `2`: filtered AO. No restart is required. |
| `r_depthPrepass` | Use `1` for AO generation. With `0`, debug mode must show the scene, not stale AO. |

For a manual check in the game console:

```text
r_ssao 1
r_ext_multisample 4
vid_restart
r_depthPrepass 1
r_ssaoAmbientOnly 0
r_ssaoDebug 1
r_ssaoDebug 2
r_ssaoDebug 0
r_ssaoAmbientOnly 1
```

Run the debug commands separately to inspect each view. `r_ssao 2` is not an
AO debug control. Use `r_ssaoDebug` to select the debug view.
White means no additional occlusion; darker pixels mean stronger occlusion.
Keep normal and specular mapping enabled. Disabling both selects fast lighting,
which does not apply AO. Forced sunlight is not required.

Broader mode is experimental. It affects direct and baked per-pixel lighting,
not HUD, sky, or unlit materials. It can darken light that already contains baked
occlusion. Ambient-only remains the default. `r_ssaoAmbientOnly` is archived;
`r_ssaoDebug` is not saved in the profile.

## Fixture

The fixture waits for map initialization in Krildor (`t2_wedge`), then uses
`exitview`, `god`, and `d_npcfreeze 1`. It does not add or remove NPCs.
The view is `setviewpos 2688 640 -60 315`, first person, FOV 80.
`r_drawentities 0` hides animated entities. The HUD and weapon are hidden.
`fixedtime 1` reduces animation changes; it does not stop all animation.
`con_notifytime -1` hides console notifications, but keeps the log markers.
A value of `0` can still show text from the current frame.

SSAO, the depth prepass, normal mapping, and specular mapping are enabled.
Auto exposure, dynamic glow, forced sunlight, and sun rays are disabled.
The fixture uses `wait 100` after view setup and `wait 20` after each
control change. These are command-buffer delays, not rendered-frame counts.
It sets `fixedtime 1` after the first wait so the player can
reach the floor before captures start. Each capture has BEGIN and END log
markers, with `wait 2` to separate capture requests from later changes.

Each process captures ambient-only lighting, broader lighting, raw AO,
filtered AO, and restored ambient-only lighting without a restart. It then
captures filtered debug mode with the depth prepass off and compares it with
the normal scene with the depth prepass off. At the end, it restores
`r_depthPrepass 1`, `r_ssaoDebug 0`, and `r_ssaoAmbientOnly 1`.
It also requests screenshots immediately before `vid_restart` and `quit`.
Both must be written. Normal screenshots capture the completed frame; shutdown
captures the last available image when no further swap can occur.

## Criteria

Results, PNG files, commands, and logs stay under `build/smoke/ssao.*`.
The test prints the measured values before each image assertion. Retain
standard output with the results. The limits below passed the controlled
MSAA 0 and 4 tests. They are not universal visual-quality targets.

- FFmpeg must decode each PNG to 640 x 480 RGB and 160 x 120 grayscale.
- Both AO views must have RGB channel differences of at most 2 for at least
  99.99% of pixels. This rejects a colored scene used as a debug view.
- Each AO view must have a grayscale range of at least 4. At least 0.1% of
  reduced pixels must be below 250. Uniform and all-white AO fail.
- Scene views must have a grayscale range of at least 16. At least 1% of
  full-size pixels must have an RGB channel difference greater than 2.
- Broader lighting must reduce grayscale by more than 2 at 0.1% or more of
  reduced ROI pixels. This fraction must exceed twice the round-trip change
  fraction at the same threshold. The mean reduction must exceed 0.01.
- The restored ambient view must have a mean absolute grayscale error of
  at most 1. At most 1% of reduced ROI pixels can differ by more than 8.
  The two prepass-off scene views must meet these same limits.
- For each AO view, compare MSAA 0 and 4 masks at grayscale below 250.
  In each direction, at most 15% of mask pixels can lack a matching pixel
  within one reduced pixel. Mask bounds can move by at most four reduced
  pixels. The mask area ratio must be between 0.5 and 2.
- Missing markers, missing images, GL errors, renderer fallback, an
  unexpected restart between captures, or a failed smoke run cause failure.

These checks permit small edge and animation differences. They do not use
exact whole-image equality. Inspect failed images and metrics before a limit
change; do not increase limits only to get a pass.

Scene comparisons use a fixed wall and floor region: source pixels
`224 <= x < 416`, `224 <= y < 448`, with the origin at the top left.
After reduction to 160 x 120, the bounds are `[56,104)` by `[56,112)`.
This region excludes console text, windows, and sky. Broader lighting,
round-trip error, and the prepass-off comparison use the same region.
AO grayscale checks and MSAA mask comparisons still use the full image,
including its left edge. The test does not select pixels from measured
differences.

## Results

The controlled captures in `build/smoke/ssao.t5edu_tb` passed with MSAA 0 and 4.
Both raw and filtered AO contained visible occlusion. Broader mode darkened
approximately 16.6% of the interior ROI by more than two grayscale levels.
The ambient-mode round trip and prepass-off comparisons had zero ROI error.
The MSAA masks had matching bounds and similar coverage.
Screenshots immediately before restart and quit also passed. Rend2 lifecycle
and shader-cache tests passed. Hardware P630 captures at 1280 x 720 with 4x MSAA
confirmed both broader lighting and the filtered debug view.
A 3840 x 2160 hardware capture with 4x MSAA also confirmed filtered AO output.

An earlier comparison failure came from console notifications, not persistent
lighting changes. Disabling notifications and using the fixed interior region
removed that interference without relaxing the tolerances.

## Limits

Static captures do not prove the absence of one-frame motion latency. A
separate motion test is required for that claim. The MSAA comparison can
detect large depth-resolve errors, but it does not prove which FBO was bound.
This fixture tests a world view, not a portal view. It does not test startup
with `r_ssao 0`. With SSAO off, debug controls must leave the scene visible;
the AO grayscale criteria would not apply to that separate test.
