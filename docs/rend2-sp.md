# Single-Player Rend2 Port

## Current Status

The opt-in `sp-rend2-port` target compiles shared Rend2 code against single-player
headers, API version 18, and renderer-side definitions. It is an object target,
not a linked or playable renderer. It does not install `rdsp-rend2` or change the
renderer used by the game. Vanilla remains the single-player renderer.

The target currently compiles:

- Allocation helpers and SP zone-allocation adapters.
- Extra math, tangent-space code, and MikkTSpace.
- The runtime GLSL parser and shader-program implementation.
- Generated built-in shader sources.

SP and MP share one shader generator. Raster sources are not copied into a
second renderer tree. The SP header path is checked at compile time. Allocation
adapters use SP signatures and tags, not casts between incompatible interfaces.

## Build Check

```bash
cmake -S . -B build/rend2-port -DBuildSPRend2Port=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/rend2-port --target sp-rend2-port --parallel 1
```

`BuildSPRend2Port` defaults to `OFF`. All builds must use one job. The SP objects,
MP Rend2 library, and SP vanilla library have passed Linux build checks. Shader
source generation has passed; GPU shader compilation and SP Rend2 rendering have
not been tested. Object compilation permits unresolved renderer dependencies.

## Next Milestones

1. Adapt SP scene submission and entity handling. Do not interpret SP entities with the MP structure layout or flag values.
2. Integrate the SP Ghoul2 array, caller-owned handles, skeleton evaluation, collision, and animation configuration. Do not use MP cleanup on SP-owned wrapper objects.
3. Complete API 18 imports and exports, registration, tagged allocation lifetime, level-load completion, and renderer restart handling.
4. Link an experimental module with unresolved-symbol checks. Verify the loaded renderer identity; fallback to vanilla must fail the Rend2 test.
5. Load a representative SP map with animated characters, sabers, effects, UI, and a cinematic. Compare fixed captures and frame times before enabling more effects.

No ray tracing is planned. Initial graphics work uses raster lighting, shadow
maps, and existing Rend2 materials and effects. A 4K framebuffer and widescreen
world FOV already work in vanilla; they do not depend on this port. Legacy menu
choices and HUD layout are separate work.
