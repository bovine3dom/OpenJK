# Sky and Fog Investigation

## Scope

This September 16, 2026 review covers SP Rend2. Vulkan remains deferred.
The user approved four implementation stages after this review. Use a separate
commit for each stage. Measure cost on this project before selecting defaults.
The source projects' performance figures do not predict performance here.

## Findings

The current sky loader uses six separate 2D images. Each image uses mipmaps and
clamp-to-edge sampling. A seamless cubemap state change cannot fix that path.
Inspect face orientation, source edges, compression, and mipmaps separately.
Higher image resolution cannot correct a discontinuous horizon.

Use one continuous environment to produce replacement sky faces. Preserve the
map's sun, colors, landmarks, and unusual planetary features. Do not enlarge each
face independently. Preserve the original assets and provide a runtime fallback.

Distance and height haze can improve the transition from terrain to sky. Use
explicit map settings and preserve combat visibility. Existing fog parameters
need calibration before use as physical density. Prevent duplicate fog and
outdoor haze inside enclosed rooms. Transparent surfaces need fog at their own
depth, not the depth of the opaque surface behind them.

A camera-aligned volume grid can store local fog density and lighting. Limit
grid size, view distance, and light count. Temporal filtering reduces noise but
can leave trails behind moving lights. Test sabers, blasters, explosions, and
the torch. Reset history after camera cuts and map changes. Use known lights
and shadow data. Bright textures do not identify the lights that produced them.

Lookup-table atmosphere rendering is a useful later option for outdoor maps.
It needs explicit map settings. A physical atmosphere does not reproduce painted
landmarks, moons, or all alien skies. Volumetric clouds require a separate
authoring and performance project.

## Implementation Order

1. Diagnose sky seams and correct sampling or asset errors.
2. Add high-resolution replacements for selected skies.
3. Add map-controlled distance and height haze.
4. Add a bounded local volumetric fog prototype.
5. Prepare a detailed procedural atmosphere plan, including `t1_sour`.

For renderer changes, run `bash scripts/build-sp.sh` with one build job. Test
stock and enhanced views, map changes, restart, transparent effects, and indoor
transitions where relevant. Record measured results and remaining limitations.

## Primary Sources

- [Ignacio Castaño: Seamless Cube Map Filtering](https://www.ludicon.com/castano/blog/articles/seamless-cube-map-filtering/).
  Describes cross-face filtering and the artifacts that edge repair can cause.
- [Epic: Volumetric Fog](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-fog-in-unreal-engine).
  Describes a camera-aligned volume, temporal trails, transparency, and grid cost.
- [Sébastien Hillaire: Sky Atmosphere reference implementation](https://github.com/sebh/UnrealEngineSkyAtmosphere).
  Accompanies the 2020 paper, *A Scalable and Production Ready Sky and Atmosphere
  Rendering Technique*. Provides lookup-table construction and reference modes.
- [Guerrilla: Nubis](https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine).
  Describes production cloud authoring, animation, and atmosphere integration.

These pages were inspected directly. No source implementation was built as
part of this review.
