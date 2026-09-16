# Volumetric Clouds: Deferred Investigation

## Status

The user deferred volumetric cloud implementation on September 16, 2026.
Current work covers map-specific atmosphere profiles and visual review.

## Candidate

Krildor (`t2_wedge`) is the first proposed cloud test. Its cloud sea occupies a
large outdoor area. The platforms provide a reference for cloud depth and scale.
The current local fog grid is not a volumetric cloud renderer.

## Proposed First Prototype

1. Use one bounded cloud layer below the platforms.
2. Author a fixed three-dimensional density field for the map.
3. Match cloud lighting to the golden sky and its light direction.
4. Render at reduced resolution with bounded view and shadow samples.
5. Provide a live stock/cloud comparison and measure GPU cost.

The intended benefits are parallax, changing silhouettes, and cloud self-shadowing.
These require per-frame integration. They are more expensive than the static sky
lookup table. Measure cost on the P630 and desktop GPU before choosing defaults.

Evaluate foreground edges, sky portals, reconstruction noise, and temporal trails.
Define an explicit composition rule for atmosphere, local fog, and clouds.
Animated weather and camera entry into clouds belong to later increments.
Retain authored thin clouds on `t1_sour` during the atmosphere profile review.

## Reference

[Guerrilla's Nubis presentation](https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine)
describes real-time cloud rendering, authoring, animation, and atmosphere integration.
Its performance figures are not measurements of this project.
