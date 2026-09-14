# Single-Player Rend2 Port

## Current Status

SP Rend2 now links and installs as `rdsp-rend2_x86_64.so` on Linux x86-64.
It is playable, but remains experimental and opt-in. The engine and launcher
still default to `rdsp-vanilla`.

The module uses native SP API 18 imports and exports. Native SP Ghoul2 retains
ownership of arrays and handles. It supplies bone evaluation, collision, Ghoul2
save data, inverse kinematics (IK), and ragdolls. The first playable path uses
CPU skinning with packed normals and tangents in Rend2 dynamic buffers.

SP and MP share Rend2 raster code, GLSL, framebuffer objects (FBOs), lighting,
materials, effects, and one shader generator. Raster sources are not copied into
a second renderer tree. No ray tracing is used or planned.

## Build and Launch

Run the normal package build from the repository root:

```bash
bash scripts/build-sp.sh
```

The script always enables both SP renderers and uses one build job. It records
the vanilla test in `smoke-result.txt` and the Rend2 test in
`smoke-rend2-result.txt`. Both tests must pass before it updates `build/ready`.

To build only the experimental renderer target:

```bash
cmake -S . -B build/rend2-sp -DBuildSPRend2=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/rend2-sp --target rdsp-rend2_x86_64 --parallel 1
```

`BuildSPRend2` defaults to `OFF` outside the build script. It replaces the removed
`BuildSPRend2Port` option and compile-only `sp-rend2-port` target. The Linux module
links with `--no-undefined`. All builds must use one job.

For desktop setup, see `README.md` in the package or `docs/development.md` in
the repository. Select Rend2 explicitly:

```bash
openjk-play +set cl_renderer rdsp-rend2
```

`cl_renderer` is archived in the profile. Later launches use the saved choice.
To return to vanilla:

```bash
openjk-play +set cl_renderer rdsp-vanilla
```

The engine can fall back to vanilla if it cannot load the selected module.
Strict Rend2 tests reject fallback and require the Rend2 identity in the log.

## Verification

Run headless tests one at a time:

```bash
OJK_SMOKE_RENDERER=rdsp-rend2 OJK_SMOKE_TIMEOUT=600 \
  bash scripts/smoke-sp.sh build/ready t2_wedge
python3 scripts/test-rend2-sp.py --package build/ready
python3 scripts/test-rend2-sp.py --package build/ready --shadows 2
python3 scripts/test-rend2-sp.py --package build/ready --shadows 3
python3 scripts/test-rend2-sp.py --package build/ready --shadows 3 --buffer-storage
OJK_SMOKE_RENDERER=rdsp-rend2 bash scripts/test-display-sp.sh build/ready
OJK_SMOKE_RENDERER=rdsp-rend2 OJK_SMOKE_TIMEOUT=600 \
  python3 scripts/test-squad-tactics.py --package build/ready
```

These checks passed with a Linux GCC build, Xvfb, and LLVMpipe:

- The `t1_sour` baseline with vanilla and Rend2, and `t2_wedge` and `hoth2` with Rend2.
- Rend2 screenshots that show NPCs, textured maps, the player, and a yellow saber.
- The lifecycle test with OpenGL debug checks: `vid_restart`, format-2 save/load in one process, and the `t2_wedge` to `t1_sour` transition.
- Lifecycle checks with stencil and projected shadows, patch stitching disabled, and persistent buffers enabled.
- A rendered, non-black 3840x2160 scene.
- All nine squad-tactics cases, including save/load with an active tactic.
- Genuine v1 save migration and a v2 save/load cycle with state checks. Files with valid checksums but invalid versions 0 and 3 were rejected.

For the migration command with `--renderer rdsp-rend2`, see `save-migration.md`
in the package or `docs/save-migration.md` in the repository.
These results use software rendering only.
The original cinematic camera was visible in initial smoke tests.
This does not verify all cinematic scenes.

## Next Milestones

Desktop feedback reports much longer loading, stutter, and input lag. SSAO with
`cg_shadows 3` received positive visual feedback. Keep those effects as a test
configuration while investigating performance; do not assume SSAO causes the
large slowdown. Hardware P630 benchmarks now run without a monitor. See
`benchmark-sp.md` for initial measurements and their limits.

1. Add a dedicated beam scene to check color, depth, and draw order. The beam fix has passed build checks only.
2. Test effects, UI, cinematics, and progression across more campaign scenes.
3. Check Rend2 on the NVIDIA GTX 1080 Ti. Record frame times, settings, driver, and resolution. Check audio and manual gameplay.

The port is not feature-complete. Both renderers support a 4K framebuffer and
widescreen world FOV. Legacy menu choices and HUD layout are separate work.
