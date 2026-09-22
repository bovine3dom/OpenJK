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

Use a separate regular spatial grid. The evaluation grid samples every second
horizontal BSP light-grid position and every vertical position. Its spacing is
128 world units on each axis.

Skip positions in solid BSP leaves or positions that render no world surfaces.
Keep an invalid marker in the regular grid so that runtime lookup stays
constant-time. Runtime trilinear interpolation uses valid corners only. If all
eight corners are invalid, search two grid positions in each direction. Use
only fallback positions in the potential visibility set. Fall back to the
original ambient light when this search finds no valid probe.

This spacing is a quality and bake-time compromise. It can miss a small room or
blend light across a thin wall. The fallback search can also produce a visible
change when its nearest valid set changes. A later version can use visibility
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
- `r_gtaoBentNormalProbes` above zero

`r_gtaoBentNormalProbes` defaults to `1`. It is a live blend from zero to one.
Zero uses the original ambient term. One uses the directional probe term. The
master bent-normal control still requires `vid_restart`.

## Bake Design

The offline in-engine baker must:

1. Load the retail map through the normal renderer.
2. Visit each non-solid probe-grid position.
3. Render six low-resolution static scene views.
4. Read linear HDR color before tone mapping.
5. Project the pixels directly into first-order irradiance coefficients.
6. Write the probe file to the profile.

The bake excludes the view model and dynamic scene entities. It does not store
cubemap images or run reflection convolution. It temporarily disables a map's
fixed sky-portal view during each capture. Without this step, the fixed portal
camera contaminates every probe with the same image. Review outdoor probes for
missing sky energy.

Use this command to bake the current map with the default grid and 16 by 16
faces:

```text
r_bakeIrradianceProbes
```

The optional arguments are horizontal stride, vertical stride, and face size.
The supported ranges are 1 through 8 for each stride and 4 through 32 for face
size. Use the collection script for the two evaluation maps:

```sh
python3 scripts/bake-irradiance-probes.py t1_sour --package build/ready
python3 scripts/bake-irradiance-probes.py kejim_post --package build/ready
```

A general client baker would also need cancellation, resume data, hardware
fallbacks, and cache management. The current command reports progress but runs
synchronously.

## Size and Time Tradeoffs

The earlier estimate of one record per unique BSP light-grid value was too low.
Spatial probes cannot use that deduplication safely.

The distributed files use horizontal stride two, vertical stride one, and 16
by 16 cubemap faces. The Intel P630 produced these results:

| Map | Grid positions | Captured positions | File size | Bake time |
| --- | ---: | ---: | ---: | ---: |
| `t1_sour` | 75,240 | 31,063 | 1,956,296 bytes | 90.3 seconds |
| `kejim_post` | 125,280 | 36,486 | 3,257,336 bytes | 138.1 seconds |

The two files use 5,213,632 bytes, or 4.97 MiB. Invalid records are retained for
direct indexing. The unpacked runtime grids use approximately 3.7 MiB and 6.2
MiB.

The capture time confirms that a synchronous client bake is too slow for a
normal map load. A background bake would submit six extra scene views for each
captured position. It would need a strict frame budget and would take much
longer than the synchronous figures. Pre-baking removes this work from normal
play.

## Evaluation

Publish the package with `scripts/build-sp.sh`. Start each test map through the
desktop updater:

```sh
openjk-play --worktree rmlui --campaign ja \
  +set cl_renderer rdsp-rend2 +set r_ssao 1 +set r_ssaoMethod 1 \
  +set r_gtaoBentNormals 1 +set r_gtaoBentNormalDirectional 0.25 \
  +devmap t1_sour
openjk-play --worktree rmlui --campaign jo \
  +set cl_renderer rdsp-rend2 +set r_ssao 1 +set r_ssaoMethod 1 \
  +set r_gtaoBentNormals 1 +set r_gtaoBentNormalDirectional 0.25 \
  +devmap kejim_post
```

Use `r_gtaoBentNormalProbes 0` and `r_gtaoBentNormalProbes 1` in the console.
The change is live. Keep the same camera and exposure. Focus on characters and
other lit entity surfaces. The probes do not change the level geometry.

Inspect these subjects:

- Faces and heads near colored openings
- Characters moving between indoor and outdoor areas
- Corners with different light colors on each side
- Narrow rooms near another bright room
- Probe-grid boundaries

Reject the method if it causes visible light leaks, abrupt color changes,
unstable brightness, or stronger mesh seams. If the result is useful but too
coarse, test horizontal stride one before changing the representation.
