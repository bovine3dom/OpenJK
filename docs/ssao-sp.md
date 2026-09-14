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
| `r_sampleShading` | Default `0`: ordinary MSAA. `1`: shade every scene sample. Values between `0` and `1` set a minimum sample fraction. Changes are live. Requires MSAA and sample-shading support. |
| `r_ssaoMethod` | Default `0`: legacy SSAO. `1`: spatial GTAO. Use `vid_restart` after a change. |
| `r_gtaoHalfRes` | Default `0`: native-resolution GTAO. `1`: calculate and filter at half width and half height, then upscale with full-resolution depth. Use `vid_restart` after a change. |
| `r_gtaoQuality` | `0` low, `1` medium (default), `2` high, `3` ultra. Changes are live. Applies to GTAO only. |
| `r_ssaoAmbientOnly` | Default `1`: apply SSAO to ambient light and IBL. `0`: apply SSAO once to all per-pixel Lightall lighting. No restart is required. |
| `r_ssaoStrength` | World strength, from `0` to `4`. Default `1`; `0` removes screen AO from world lighting. |
| `r_ssaoRadius` | World radius multiplier, from `0.05` to `4`. Default `1`. |
| `r_ssaoViewModel` | Default `1`: enable first-person weapon self-occlusion. `0` disables it. |
| `r_ssaoViewModelStrength` | Weapon strength, from `0` to `4`. Default `0.5`; `0` removes weapon AO from lighting. |
| `r_ssaoViewModelRadius` | Weapon radius multiplier, from `0.05` to `4`. Default `0.05`. |
| `r_ssaoDebug` | Default `0`: scene. `1`: raw world AO. `2`: filtered world AO. `3`: weapon mask. `4`: weapon AO. No restart is required. |
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

The strength, radius, and weapon controls change live and are saved in the
profile. Radius is a multiplier, not a distance in meters. Projection scaling
keeps the reference sampling footprint consistent when FOV or aspect ratio
changes. The legacy default reproduces the old kernel at 80-degree horizontal
FOV and 4:3 aspect. GTAO uses a different radius calculation.

## Spatial GTAO and Sample Shading

Use these settings to enable GTAO with ordinary MSAA:

```text
r_ssao 1
r_ssaoMethod 1
r_gtaoQuality 1
r_ext_multisample 4
r_sampleShading 0
vid_restart
```

Keep the existing strength, radius, and ambient-only settings as a starting
point. The same strength value can look different with the two AO methods.
World and weapon AO remain separate. The debug modes work with both methods.

GTAO reconstructs view-space positions and surface normals from depth. It
calculates occlusion along hemisphere slices with a cosine-weighted integral.
By default, calculation and filtering use full display resolution. Two
depth-aware, five-tap passes filter the result. The sampling pattern is fixed in
screen space. There is no frame history, temporal jitter, or TAA requirement.

This is an independent GLSL implementation of the GTAO approach described by
[Jimenez et al. (2016)](https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf).
It is not a port of the complete XeGTAO implementation. It samples the original
depth image directly; it does not use a depth mip chain or bent normals.
Thin surfaces and hidden geometry can still produce screen-space AO artifacts.

| Quality | Slices | Samples per side | Total horizon samples per pixel |
| --- | ---: | ---: | ---: |
| Low (`0`) | 2 | 1 | 4 |
| Medium (`1`) | 2 | 2 | 8 |
| High (`2`) | 2 | 4 | 16 |
| Ultra (`3`) | 3 | 4 | 24 |

Medium uses the original Low sampling settings. The new Low uses fewer steps.
The original 48-sample Ultra preset was removed after desktop performance
feedback. Set `r_gtaoQuality 1` in an existing profile to select the new default.
Saved values are not reset when the renderer default changes.

Normal reconstruction and filtering require additional texture reads.
Resolution is independent of the quality preset. A larger radius is not a
higher quality preset.

For half-resolution GTAO, set `r_gtaoHalfRes 1`, then use `vid_restart`.
AO calculation and both filter passes use half width and half height. A final
pass compares full-resolution depth with the four nearby low-resolution samples.
It rejects samples across depth edges and restores native-resolution output.
World and weapon depth remain separate. Odd dimensions round up for the smaller
buffers. The renderer log reports the calculation and output dimensions.

Half resolution reduces the AO pixel count to about one quarter. The extra
upsampling pass and full-resolution depth work limit the total speed increase.
Fine contact detail can be lost, especially with a small weapon radius.
The world radius default remains `1`; the weapon radius default is `0.05`.
Existing profiles retain saved values. Set `r_ssaoViewModelRadius 0.05` explicitly
to apply the new default to an existing profile.

```sh
python3 scripts/test-modern-rendering.py --half-res 1
python3 scripts/test-modern-rendering.py --half-res 1 --width 1279 --height 719
python3 scripts/test-ssao-weapons.py --hardware --method 1 --half-res 1 --msaa 4 --width 1280 --height 720
```

The weapon isolation fixture sets radius `1` for its controlled comparisons.
The modern-rendering fixture checks the fresh-profile radius defaults.

Half-resolution checks passed at 1280 x 720 and 1279 x 719 on the P630 with
4x MSAA. Weapon separation, radius controls, firing, restart, and save/load also
passed. In two 8-second runs per mode, the verified 720p weapon benchmark
measured median throughput of 37.56 FPS at native AO resolution and 46.46 FPS
at half resolution. Both used Medium GTAO and ordinary 4x MSAA. Results are in
`build/benchmark-sp/rdsp-rend2.8wmbgi6e` and `.0fc2on9e`.

Full sample shading evaluates materials at each MSAA sample location, including
the interiors of polygons. It applies to the main scene draw list and its depth
prepass. This includes alpha-tested depth. The renderer disables the state after
each draw list. HUD rendering and single-sample post-processing retain their
own sampling rates. Texture mip bias is unchanged.

This mode uses the GPU's multisample pattern. It does not guarantee NVIDIA's
driver SGSSAA pattern. It does not supersample the AO calculation or every
post-process. The extension log reports whether sample shading is available.
Without support, or with MSAA disabled, the setting has no effect.

### Validation

```sh
python3 scripts/test-ssao-sp.py --method 1 --sample-shading 1
python3 scripts/test-ssao-weapons.py --hardware --method 1 --sample-shading 1 --width 1280 --height 720 --fov 100
python3 scripts/test-modern-rendering.py
python3 scripts/test-modern-rendering.py --msaa 0
python3 scripts/test-rmlui-reticle.py build/ready --modern --hardware
```

The modern-rendering test uses headless hardware EGL. It checks four GTAO
presets, sample-shading enable/disable, and a camera return. It also saves five
one-degree camera steps for visual inspection. These captures are not a
continuous motion test or a measurement of input latency.

Camera steps wait for new snapshots. `fixedtime 1` slows simulation time, so
short command-buffer waits can capture the previous camera position. The return
comparison uses a fixed wall region. Sky textures and character animation can
still change elsewhere in the image.

At 1280 x 720 on the P630, all presets produced AO. With 4x MSAA, sample shading
changed mean grayscale by about 0.286 levels; the restore error was about 0.007.
With MSAA disabled, the change was below fixture noise. The camera return had
zero error in the wall region. These checks prove a rendering change and state
restoration, not a universal improvement in image quality.

World GTAO checks passed with software MSAA 0 and 4. Weapon checks passed on
hardware at 1280 x 720, FOV 100, with MSAA 0 and 4. They covered firing, weapon
changes, restart, and save/load. Legacy world AO regression checks also passed.

See [benchmark-sp.md](benchmark-sp.md) for performance results and GPU timing.

## First-Person Weapons

Weapons use a separate depth image for AO. That pass uses normal depth rather
than the compressed depth used to keep the visible gun in front of scenery.
The visible gun retains its depth hack. Weapon geometry is omitted from world
AO, and the weapon AO pass contains no world geometry. This prevents either
layer from darkening the other.

The depth layer includes opaque weapon geometry. Weapon AO applies to lit
weapon surfaces regardless of `r_ssaoAmbientOnly`.
Unlit effects are unchanged. The default weapon strength is deliberately lower
than world strength. No weapon AO pass runs when no viewmodel is present.
Its debug image is then white. Debug mode 3 shows black weapon geometry on a
white background; mode 4 shows its self-occlusion. Debug images show the generated
mask, before the lighting-strength adjustment.

Run the weapon checks separately:

```sh
python3 scripts/test-ssao-weapons.py --package build/ready
python3 scripts/test-ssao-weapons.py --package build/ready --hardware --width 1280 --height 720
python3 scripts/test-ssao-weapons.py --package build/ready --hardware --width 1280 --height 720 --fov 100 --msaa 4
```

The hardware option uses offscreen EGL and rejects software renderers.
The tests check world/weapon separation, wall independence, strength, radius,
disable/re-enable, hidden weapons, firing, weapon changes, restart, and save/load.
Small image differences are allowed for idle weapon motion. These checks do not
replace campaign tests of every weapon, effect, or camera mode.

These checks passed at 640 x 480 in software and at 1280 x 720 on the P630,
with MSAA 0 and 4. A hardware run with FOV 100 and MSAA 4 also passed.
The world test now checks zero strength, increased strength, and radius changes.
At the reference view, zero world strength matches the no-AO lighting result,
and strength 2 produces more darkening than strength 1.

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
