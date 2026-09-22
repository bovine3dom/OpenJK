# Directional Irradiance Probes

## Purpose

Directional irradiance probes supply low-frequency environment light to dynamic
models. GTAO bent normals select light from the visible direction. Material
normals continue to control direct lighting and the BRDF.

The distribution covers all 60 single-player campaign maps:

- Jedi Academy: 34 maps
- Jedi Outcast: 26 maps

The build stores the probe files in `irradiance-probes.pk3`. The archive uses
normal ZIP compression. `scripts/play-sp.sh` transfers it with the other package
data. Multiplayer maps are outside this single-player rollout.

## Distribution Decision

Use pre-baked probes for the campaign distribution. Do not make each player
bake a map before play.

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

### Asset Possession Gate

Do not describe probe encryption as proof of ownership. Software cannot prove
that a person owns a map. It can only check that the client can read the exact
BSP data.

A level checksum is not a secret. A person can copy the checksum or extract the
decryption procedure from the open-source client. Encryption also does not
change the legal status of generated data. It adds format, key-management, and
failure-mode complexity.

The current format is not encrypted. The launcher requires the retail assets,
and each probe file contains the source BSP checksum. The renderer rejects the
file when the loaded BSP does not match. This is an asset compatibility check,
not an ownership check.

If a legal review requires a stronger possession gate, derive an authenticated
per-map key from the full BSP bytes and the format version. Do not store that
key or the full derivation input in the probe archive. Compress each map before
encryption. This design would make the archive unusable until the exact retail
BSP is present, but it would still be an access check and not legal proof.

## Spatial Layout

Do not store one probe for each unique BSP light-grid record. The BSP light-grid
array reuses equal records at unrelated positions. A directional result from
one reused position would be wrong at the other positions.

Use a separate regular spatial grid. The campaign grid samples every second
horizontal BSP light-grid position and every vertical position. BSP maps can
set different light-grid sizes. The resulting horizontal spacing ranges from
128 to 512 world units. Vertical spacing ranges from 128 to 384 world units.

Skip positions in solid BSP leaves or positions that render no world surfaces.
Keep an invalid marker in the regular grid so that runtime lookup stays
constant-time. Runtime trilinear interpolation uses valid corners only. If all
eight corners are invalid, search two grid positions in each direction. Use
only fallback positions in the potential visibility set. Fall back to the
original ambient light when this search finds no valid probe.

These spacings are a quality and bake-time compromise. A coarse map grid can
miss a small room or blend light across a thin wall. The fallback search can
also produce a visible change when its nearest valid set changes. A later
version can use visibility
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

`r_gtaoBentNormals` and `r_gtaoBentNormalProbes` default to `1`.
`r_gtaoBentNormalDirectional` defaults to `0.25`. The probe control is a live
blend from zero to one. Zero uses the original ambient term. One uses the
directional probe term. The master bent-normal control still requires
`vid_restart`.

An existing profile keeps its archived values. Run `exec rend2-defaults.cfg` to
apply the new defaults to that campaign profile.

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
size. Use the collection script for one map or the full campaign set:

```sh
python3 scripts/bake-irradiance-probes.py t1_sour --package build/ready
python3 scripts/bake-irradiance-probes.py kejim_post --package build/ready
python3 scripts/bake-irradiance-probes.py --all --skip-existing --package build/ready
```

The batch command gets the campaign map inventory from
`scripts/atmosphere-catalogue.json`. It continues after a failed map. Run the
same command again to resume the batch.

A general client baker would also need cancellation, resume data, hardware
fallbacks, and cache management. The current command reports progress but runs
synchronously.

## Size and Time Tradeoffs

The earlier estimate of one record per unique BSP light-grid value was too low.
Spatial probes cannot use that deduplication safely.

The distributed files use horizontal stride two, vertical stride one, and 16
by 16 cubemap faces. The Intel P630 produced these campaign totals:

| Campaign | Maps | Grid positions | Captured positions | Raw size | Bake time |
| --- | ---: | ---: | ---: | ---: | ---: |
| Jedi Academy | 34 | 3,818,695 | 1,883,087 | 99,287,974 bytes | 8,095.1 seconds |
| Jedi Outcast | 26 | 2,659,528 | 738,057 | 69,149,184 bytes | 3,813.7 seconds |
| Total | 60 | 6,478,223 | 2,621,144 | 168,437,158 bytes | 11,908.8 seconds |

The complete bake took 3 hours and 18 minutes. Individual maps ranged from less
than one second to 28 minutes. Invalid records are retained for direct indexing.
Only the current map is unpacked at runtime. The largest grid uses approximately
13.1 MiB of runtime memory.

ZIP compression reduces the 160.6 MiB of raw data to a 51,343,535-byte package,
or 49.0 MiB. The archive also contains a manifest with each map checksum, grid
shape, record count, and SHA-256 probe hash.

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
