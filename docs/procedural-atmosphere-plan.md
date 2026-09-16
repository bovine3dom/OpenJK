# Procedural Atmosphere Plan

## Goal and Status

Add a continuous sky and consistent distant haze to selected outdoor SP maps.
Use `t1_sour` as the first art and integration target. Preserve campaign lighting,
landmarks, visibility, and scripted camera behavior. This document is a plan.
Procedural atmosphere is not implemented.

The current foundation provides seamless sky cubes, selected source-derived
2048-pixel skies, bounded analytic haze, and an optional local fog grid.
See [SP Sky and Fog](sky-fog-sp.md) for controls, tests, and measured costs.

## Verified t1_sour Inputs

The local stock archives contain these inputs:

| Input | Value | Consequence |
| --- | --- | --- |
| BSP sky surface | `textures/skies/desert` | Target this sky material explicitly. |
| Shader sky image prefix | `textures/skies/desert` | Keep these faces as the live fallback. |
| Shader cloud height | `512` | This is a legacy cloud parameter, not an atmosphere height. |
| Shader sun declaration | None | The profile must specify a sun direction. |
| Worldspawn | `_noshadersun 1` | Do not infer the scene sun from a renderer default. |
| Worldspawn color | `1.000000 0.882353 0.666667` | Use as an art reference, not a measured spectrum. |
| Sky portal origin | `2360 -6048 -64` | The portal has a different origin from the main camera. |
| Player-start entity | `9080 -4896 48`, yaw `225` | An authoring anchor for the first capture route. |
| `new_player_start` reference tag | `4693 -1624 55`, yaw `225` | A second authoring anchor after scripted setup. |

The last two rows are BSP anchors. They are not verified screenshot positions.
Record actual `viewpos` values after the opening sequence before making tests.

## Visual Direction

Start with a fixed dusty daylight profile. Give the upper sky a restrained
gradient. Add a warm horizon and directional haze around the sun. Preserve the
city silhouette and the contrast of nearby enemies. Keep the sun fixed to match
the baked scene. A moving sun or day/night cycle needs a separate lighting plan.

Inspect all six source faces before replacing their background. Inventory painted
clouds, discs, mountains, and other landmarks. Preserve required features as
explicit layers. If the source contains multiple suns, record each disc and
direction. Start scattering with one dominant source; add a second source only
after the one-source result is stable. Do not add a second disc over a painted one.

The first review should compare the stock sky, the procedural sky alone, and the
procedural sky with aerial perspective. Keep exposure fixed for these comparisons.

## Rendering Design

Use the lookup-table approach described by
[Hillaire's reference project](https://github.com/sebh/UnrealEngineSkyAtmosphere).
Its [ray-marching source](https://github.com/sebh/UnrealEngineSkyAtmosphere/blob/master/Resources/RenderSkyRayMarching.hlsl)
contains transmittance, multiple scattering, sky-view, and camera-volume paths.
The source was inspected directly. It is a design reference, not a drop-in port.

### Optical Model

- Use Rayleigh scattering for the broad sky gradient.
- Use Mie scattering and absorption for dust and sun haze.
- Make absorption, density heights, ground reflectance, and sun intensity explicit.
- Store light and transmission in linear color space. Apply the existing exposure
  and tone-mapping path once, after scene composition.
- Start with fixed map parameters. Do not require temporal accumulation for a
  stable sky or a static atmosphere.

Keep the implementation in the current raster renderer. The reference uses a
compute reduction for multiple scattering. Use bounded fragment passes or an
offline reduction for our first version. Do not require a newer GL context.

### Initial Tables

These dimensions are proposed starting points, not measured quality settings.

| Table | Initial size | Update rule |
| --- | --- | --- |
| Transmittance | 256×64 | Rebuild after optical parameters or planet radii change. |
| Multiple scattering | 32×32 | Rebuild after relevant medium or ground parameters change. |
| Sky view | 192×108 | Rebuild after sun elevation or camera altitude changes. Camera yaw samples the same table. |
| Aerial perspective | 32×18×32 | Rebuild for each changed main view. Share it with the associated sky view. |

Use RGBA16F initially. Aerial perspective needs separate RGB scattering and RGB
transmission storage. The local fog prototype's single transmission channel is
not sufficient for wavelength-dependent atmospheric extinction. Keep the table
memory budget below 8 MiB, including temporary targets.

Quantize altitude updates only after measuring visible error. Cache tables by
their physical parameters. Do not rebuild camera-independent tables each frame.
Reset all view-dependent state on map change, renderer restart, and profile reload.

### Coordinate and Portal Rules

Define one map-to-atmosphere transform. It needs a reference ground height, world
up, planet radii, and a world-unit scale. Calibrate the scale against map geometry.
Treat all initial planet values as artistic settings, not canonical measurements.

Use camera-relative positions for nearby geometry. Avoid subtracting large
planet-centered float positions to recover small height differences. Check horizon
intersections and camera heights near the ground for numerical instability.

In `t1_sour`, both sky views must use the main camera's atmosphere altitude and
view direction. The portal origin is a scene-construction offset. It is not a
second physical observer on the planet.

Keep existing portal city geometry. Define an explicit distance transform for
that geometry before applying aerial perspective. Derive its scale from the
authored portal, then verify it with matched camera views. Do not use the raw
distance to `2360 -6048 -64` as an atmospheric path length.

### Composition and Material Coverage

1. Draw atmosphere only where the existing sky material is visible.
2. Draw required authored sky layers and discs with explicit transmission rules.
3. Apply aerial perspective at each opaque surface's world position.
4. Apply it at each transparent surface's own depth. Additive light receives
   attenuation without added background scattering.
5. Keep UI, first-person depth-hack geometry, shadow passes, and material debug
   outputs outside this composition.

The current haze code supports single-pass materials and preserves legacy fogged
and multipass materials. Expand coverage deliberately. Audit windows, water,
decals, particles, saber blades, scopes, refraction, and additive effects. A
post-process based only on opaque depth cannot solve their different depths.

Atmosphere and local fog must have one integration policy. For disjoint regions,
compose front-to-back scattering and transmission in distance order. For overlap,
combine extinction and scattering coefficients in a shared integration step.
Do not apply two independent full-path haze effects to the same air. Keep a
compatibility path for maps with authored legacy fog until their conversion is
verified.

### Outdoor and Indoor Boundaries

Sky visibility is sufficient to select sky pixels. It does not identify which
parts of a surface-to-camera ray are outdoors. Build explicit outdoor regions
and interior exclusions for `t1_sour`. Use a conservative spatial representation
that can clip the integration path at walls and openings.

Test an indoor camera looking through a doorway, an outdoor camera looking inside,
and a camera crossing the opening. Avoid a global camera-level on/off switch,
which would make the entire view change at the doorway. The existing haze box
format is a first authoring tool, not a complete enclosure solution.

### Lighting

The first sky pass changes the visible sky only. It does not add a second sun
light to baked surfaces. Add sky-driven ambient or reflection updates as a later
explicit mode after comparing them with existing lightmaps and material lighting.

The local fog prototype already samples the torch shadow map. Use that as a
reference for known-light integration. Before adding sun shafts, verify the
directional shadow maps' coverage, resolution, and lifetime. A bright sky pixel
is not a light source definition. Keep volumetric clouds as a separate project.

## Proposed Controls and Profile

Use a versioned `maps/t1_sour.atmosphere` profile with named fields. The flat
numeric haze profile is too limited for planet parameters and portal transforms.
Validate finite values, positive radii, atmosphere thickness, density ranges,
nonzero directions, and table-size limits. A missing or invalid profile must
select the stock sky.

Proposed live controls:

- `r_atmosphere 0`: stock sky and current effects.
- `r_atmosphere 1`: procedural sky only.
- `r_atmosphere 2`: sky plus aerial perspective.
- `r_atmosphereDebug`: table, path-length, region, and transmission views.
- `r_atmosphereReload`: reload and validate the current map profile.

The base side of `r_compareEnhancements` must select stock rendering. Preserve
the user's atmosphere settings while comparison is active. Do not store renderer
tables or profile state in campaign saves.

## Implementation Commits and Acceptance Criteria

### A. Capture Route and Profile Contract

- Capture `t1_sour` after the opening sequence, in streets, in interiors, and at
  long sight lines. Include scripted sky views and a doorway transition.
- Record source assets, sky layers, portal scale, sun direction, and exposure.
- Add the versioned profile parser and live stock fallback.
- Check missing, malformed, and non-finite profiles, plus map/load/restart.

Accept when the profile has no visible effect with atmosphere disabled and all
capture positions are repeatable.

### B. Sky-Only Optical Tables

- Add the transmittance, multiple-scattering, and sky-view passes.
- Integrate the sky shader without changing surface lighting.
- Add horizon, sun-direction, and table debug views.
- Test camera rotation, altitude changes, and the portal view.

Accept when there are no face seams, horizon bands, duplicate sun discs, or
portal-origin jumps. Confirm that unchanged parameters reuse the tables.

### C. t1_sour Art Profile

- Match the existing fixed daylight and preserve required source layers.
- Tune dust, horizon color, and sun haze with fixed exposure.
- Compare gameplay silhouettes at the recorded long sight lines.

Accept after desktop review of the paired captures and a short playable route.
Keep the profile opt-in until this review is complete.

### D. Aerial Perspective and Enclosures

- Add colored scattering/transmission volumes and surface-depth sampling.
- Implement the portal distance transform and outdoor path clipping.
- Cover transparent materials and integrate the local fog policy.
- Check first-person weapons, saber effects, the torch, water, and refraction.

Accept when interior walls remain clear, outdoor scenery through openings retains
depth, portal geometry matches the main scene, and fog is not applied twice.

### E. Performance and Campaign Release

- Measure sky-only and full modes at 720p and 1080p on the P630.
- Measure the same routes on the GTX 1080 Ti when the desktop is available.
- Check load time, table rebuild spikes, steady-state GPU time, and memory.
- Run both renderer smoke tests, live comparison, save/load, renderer restart,
  camera cuts, FOV changes, MSAA 0/4, and the `t1_sour` route.
- Add a second outdoor map only after the first profile passes.

Initial P630 targets are at most 0.5 ms for steady-state sky-only rendering at
720p and at most 1.5 ms for sky plus aerial perspective at 1080p. These are
engineering targets, not measured results or release promises. Separate one-time
table work from steady-state cost. Reduce table size or update frequency only
after checking image error and motion stability.

Publish each renderer increment through `bash scripts/build-sp.sh` with one
build job. Record results in `docs/sky-fog-sp.md`. Make one commit per increment.

## First Next Action

Build the `t1_sour` capture route and inspect its six desert sky faces. Establish
the sun direction, retained layers, and portal distance transform before adding
the optical shaders. This makes the first prototype specific enough for a useful
visual review.
