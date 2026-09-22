# Directional Irradiance Probes

## Purpose

Directional irradiance probes supply low-frequency environment light to dynamic
models. GTAO bent normals select light from the visible direction. Material
normals continue to control direct lighting and the BRDF.

The first evaluation covers these maps:

- Jedi Academy: `t1_sour`
- Jedi Outcast: `kejim_post`

The build package contains the probe files. `scripts/play-sp.sh` transfers the
files with the other package data.

## Distribution Decision

Use pre-baked probes for the evaluation. Do not make each player bake a map
before play.

A synchronous client bake can add seconds or minutes to a map load. A
background bake removes the load pause, but it consumes GPU time, submits many
extra scene views, and can cause frame-time spikes. It also produces an
incomplete cache until the bake ends. A scheduler, a work budget, pause and
resume support, and cache invalidation would be necessary for a production
background baker.

Pre-baked data has these benefits:

- Map loads stay predictable.
- All players use the same lighting data.
- A developer can inspect the result before release.
- Runtime work is limited to probe interpolation and shader evaluation.

Pre-baked data has these costs:

- Each supported map needs a generated file.
- A changed BSP or bake algorithm needs a new file.
- Generated files increase the package size.
- Distribution can require a separate copyright review.

The generated data contains low-order lighting coefficients. It does not
contain textures, geometry, scripts, sounds, or a playable map. It is not useful
without the matching game map and renderer. These facts can reduce the
practical distribution risk, but they do not provide a legal conclusion.
Obtain legal advice before public distribution.

## Spatial Layout

Do not store one probe for each unique BSP light-grid record. The BSP light-grid
array reuses equal records at unrelated positions. A directional result from
one reused position would be wrong at the other positions.

Use a separate regular spatial grid. The evaluation grid samples every third
BSP light-grid position:

- Horizontal spacing: 192 world units
- Vertical spacing: 384 world units

Skip positions in solid BSP leaves. Keep an invalid marker in the regular grid
so that runtime lookup stays constant-time. Runtime trilinear interpolation
uses valid corners only. It falls back to the original ambient light when no
valid corner is available.

This spacing is a quality and bake-time compromise. It can miss a small room or
blend light across a thin wall. A later version can use a finer grid, visibility
cells, adaptive placement, or probe tetrahedra.

## Probe Representation

Use first-order RGB spherical harmonics. Store four coefficients for each color:

- Constant term
- World X term
- World Y term
- World Z term

The baker integrates the six cubemap faces and applies the cosine-kernel factors.
The runtime shader evaluates the resulting linear function in the GTAO bent-normal
direction.

Store each coefficient as an IEEE 754 binary16 value. Each grid record uses 26
bytes: 24 coefficient bytes and a 16-bit valid flag.

The file header contains:

- Magic and format version
- Source BSP checksum
- Grid dimensions
- Grid origin and spacing
- Total and valid record counts

Reject a file when its checksum, dimensions, origin, spacing, size, or version
does not match the loaded map.

## Lighting Calibration

Do not add the probe result on top of the existing ambient term. That operation
would duplicate indirect energy.

For each entity, preserve the luminance of the existing light-grid ambient
term. Scale the directional probe result to this luminance, then replace the
ambient term by the selected blend amount. This method preserves the authored
local brightness while it adds directional color and visibility.

The existing directed-light term remains separate. `r_gtaoBentNormalDirectional`
continues to control GTAO visibility for that term.

## Runtime Controls

The probe option requires all of these conditions:

- Rend2 single-player renderer
- GTAO
- `r_gtaoBentNormals 1`
- A valid probe file for the current map
- A nonzero probe blend control

A live blend control must support direct comparison between the original
ambient term and probe lighting. A value of zero must reproduce the original
ambient path.

## Bake Design

The offline in-engine baker must:

1. Load the retail map through the normal renderer.
2. Visit each non-solid probe-grid position.
3. Render six low-resolution static scene views.
4. Read linear HDR color before tone mapping.
5. Project the pixels directly into first-order irradiance coefficients.
6. Write the probe file to the profile.

The bake must exclude the view model and dynamic scene entities. It must not
store cubemap images or run reflection convolution.

A developer command is sufficient for the first evaluation. A general client
baker would also need progress reporting, cancellation, resume data, hardware
fallbacks, deterministic settings, and cache management.

## Size and Time Tradeoffs

The earlier estimate of one record per unique BSP light-grid value was too low.
Spatial probes cannot use that deduplication safely.

At stride three, the two evaluation maps contain approximately this many grid
positions before solid positions are skipped:

| Map | Grid positions | Non-solid positions |
| --- | ---: | ---: |
| `t1_sour` | 11,210 | 5,757 |
| `kejim_post` | 18,720 | 7,915 |

The packed files are approximately 285 KiB and 475 KiB before general file
compression. Invalid records are retained for direct indexing.

A finer stride-two grid would contain 40,128 and 62,640 positions. It would use
approximately 1.0 MiB and 1.6 MiB, but it would require about 3.4 times as many
captures.

Bake time depends mainly on scene submission and readback, not only pixel count.
Measure the real baker before setting a production budget. Pre-baking removes
this cost from normal play.

## Evaluation

Compare the same camera and exposure with the probe blend at zero and one.
Inspect these subjects:

- Faces and heads near colored openings
- Characters moving between indoor and outdoor areas
- Corners with different light colors on each side
- Narrow rooms near another bright room
- Probe-grid boundaries

Reject the method if it causes visible light leaks, abrupt color changes,
unstable brightness, or stronger mesh seams. If the result is useful but too
coarse, test stride two before changing the representation.
