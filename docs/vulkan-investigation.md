# Vulkan Fork Investigation

## Status

Vulkan implementation remains deferred at the user's request. Keep Rend2 as the
current renderer. This September 16, 2026 review inspected repository files,
branch descriptions, and commit messages. It did not build or benchmark the forks.

## Sources Inspected

[JKSunny/EternalJK](https://github.com/JKSunny/EternalJK) is the main source for
this review. Its README states that the Vulkan backend is based on Quake3e.
The inspected `master` revision was
[`b50059618803346a918f57220f856f1d0651dc39`](https://github.com/JKSunny/EternalJK/tree/b50059618803346a918f57220f856f1d0651dc39).

| Branch or project | Finding | Relevance |
| --- | --- | --- |
| EternalJK `master` | Contains `codemp/rd-vulkan`. Its CMake target is an MP Vulkan renderer. | A backend foundation; SP support is not established. |
| EternalJK [`pbr`](https://github.com/JKSunny/EternalJK/tree/pbr) | The README describes Rend2-derived normal, roughness, metallic, and specular mapping. The shader tree includes PBR and skinning code. | Useful for mapping our materials to Vulkan. |
| EternalJK [`vulkan-resource-opt`](https://github.com/JKSunny/EternalJK/tree/vulkan-resource-opt) | A memory-management refactor adds image scratch storage, pools, and a Rend2-derived cache manager. The author marks stability as uncertain. | Study allocation and resource lifetime; validate each proposed transfer. |
| EternalJK [`prototype-pbr-bindless`](https://github.com/JKSunny/EternalJK/tree/prototype-pbr-bindless) | Contains descriptor and bindless-rendering experiments. | A source for future binding and submission experiments. |
| EternalJK [`prototype-beta-model-instancing`](https://github.com/JKSunny/EternalJK/tree/prototype-beta-model-instancing) | Groups MD3 and Ghoul2 draws by model and rendering state. | Study selective batching rather than assuming all models benefit. |
| [TaystJK](https://github.com/taysta/TaystJK) | Includes `rd-vulkan` and lists EternalJK-Vulkan as an upstream project. | Compare integrated changes with the experimental branches. |

Relevant inspected commits include the
[`pbr` render-pass refactor](https://github.com/JKSunny/EternalJK/commit/d7f88321e1),
the [resource-management refactor](https://github.com/JKSunny/EternalJK/commit/aa27867add),
and the [instancing experiment](https://github.com/JKSunny/EternalJK/commit/15ac1b4dda).
Branch names can change; use the recorded revisions when reproducing the review.

## Concrete Code Findings

- [`vk_vbo.cpp`](https://github.com/JKSunny/EternalJK/blob/b50059618803346a918f57220f856f1d0651dc39/codemp/rd-vulkan/vk_vbo.cpp) uses device-local mesh buffers and host-visible staging buffers. Dynamic staging buffers remain mapped. Static upload paths also create temporary staging resources.
- [`vk_frame.cpp`](https://github.com/JKSunny/EternalJK/blob/b50059618803346a918f57220f856f1d0651dc39/codemp/rd-vulkan/vk_frame.cpp) contains render-pass dependencies, frame-fence waits, presentation handling, and resource-release paths.
- [`vk_pipelines.cpp`](https://github.com/JKSunny/EternalJK/blob/b50059618803346a918f57220f856f1d0651dc39/codemp/rd-vulkan/vk_pipelines.cpp) uses specialization constants and a Vulkan pipeline cache when creating graphics pipelines.
- [`vk_init.cpp`](https://github.com/JKSunny/EternalJK/blob/b50059618803346a918f57220f856f1d0651dc39/codemp/rd-vulkan/vk_init.cpp) creates that cache without initial data and destroys it at shutdown. These inspected paths do not establish persistent disk caching.

The instancing commit reports gains with many identical models and regressions
in complex scenes with different models. Those are the author's measurements.
They were not reproduced here. The result supports testing selective instancing
with a fallback, rather than replacing every draw path at once.

## Suggested Follow-up

When implementation work resumes:

1. Compare EternalJK `master`, the PBR branch, and TaystJK's integrated backend.
2. Audit allocation, uploads, frame synchronization, and draw submission.
3. Test useful changes individually in Rend2 where applicable. Preserve the
   current visual settings and compare the same scenes.
4. If measurements support a Vulkan prototype, adapt the existing backend into
   an isolated SP module before transferring the remaining rendering passes.

Our renderer already uses static GPU mesh buffers and frame-slot bone palettes.
Linked OpenGL programs also survive soft map resets. Evaluate differences from
those systems before selecting work to copy.

A Vulkan prototype would still need native SP API and Ghoul2 integration, CPU
fallbacks, campaign effects, RmlUi, save/load, map changes, and renderer restart.
It would also need our GTAO, capsule shadows, skin diffusion, SMAA, soft particles,
torch shadows, and live comparison mode. The PBR branch's material support does
not establish parity with that complete set.

Current measurements show substantial GPU-side cost as resolution increases.
Vulkan can reduce CPU and driver overhead, but shader and bandwidth costs still
need separate work. Use `benchmark-sp.md` and target-hardware traces to judge a
prototype. No performance gain from these forks is claimed by this review.
