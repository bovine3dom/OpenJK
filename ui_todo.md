# Incremental UI Plan

## Agreed Direction

- Use a minimal visual style that retains the Jedi Academy theme.
- Use crisp text, geometric panels, thin borders, restrained highlights, and consistent spacing.
- Support independent UI scale and aspect-correct layouts. A larger framebuffer alone does not make the UI sharp.
- Replace low-resolution artwork where needed. A UI library cannot recover missing image detail.
- The Force wheel slows game time while held. Release selects a power. The normal activation button uses it.
- Keep direct Force bindings. Cancel the wheel on focus loss, death, or a cinematic transition.
- Update wheel animation and input in real time. Restore the appropriate game speed on close, not a fixed timescale of 1.
- First MVP: replace the stretched reticle with an aspect-correct reticle. Keep collision-based movement and enemy/friendly identification.

## Reticle MVP Status

- [x] Set the default dot scale to 0.75.
- [x] Replace the bottom-right SP resource panel with contextual Force and ammo rings and a stance arc.
- [x] Replace the bottom-left panel with paired health and shield arcs. Retain a faint critical-health arc at 25% or below.
- [ ] Choose a status-check binding and add a held view with exact resource values.
- [ ] Check ring size, opacity, fade timing, and stance readability during human play.

- [x] Integrate RmlUi 6.3 into the Jedi Academy SP client with static dependencies.
- [x] Replace normal reticle artwork in vanilla and Rend2. Keep the existing collision and target-color logic.
- [x] Add `cg_rmluiReticle` for legacy fallback and `cg_rmluiReticleScale` for independent size control.
- [x] Destroy the RmlUi context before renderer shutdown and recreate it after registration.
- [x] Test 4:3 and 16:9 output, equal pixel dimensions, scale, hiding, fallback, and `vid_restart` on both renderers.
- [ ] Check collision movement, enemy/friendly colors, Force hints, vehicles, pickup animation, and reduced view size in gameplay.
- [ ] Check the result on hardware graphics drivers.

The MVP supports geometry, rectangular clipping, and generated font textures.
General UI screens and image-file loading are not yet supported. MP, Jedi Outcast, turret
artwork, and the Force corona still use their existing paths.
See [MVP use and tests](docs/rmlui-reticle.md) and
[dependency records](docs/rmlui-dependencies.md).

## Migration

The first SP Force wheel uses a normal controls-menu binding. G is the initial
default when free. Middle mouse can be assigned in the menu. See
[Force wheel controls and tests](docs/force-wheel.md).

- [x] Keep movement active while the Force wheel is open. Block combat actions and camera turning.
- [x] Render the wheel label with bundled IBM Plex Mono through FreeType at native pixel size.

RmlUi is the leading candidate. Prove its integration before replacing many screens.
Keep existing screens available during migration.

- [ ] Add a renderer-neutral drawing interface for vanilla and Rend2, with explicit clipping and resource lifetime.
- [ ] Add filesystem, localization, font, and input adapters. Keep the existing event loop, cvar store, and binding store.
- [ ] Give each screen one UI owner. Transfer focus explicitly between old and new menus.
- [ ] Prove a simple test panel across resolutions, UI scales, focus changes, and `vid_restart`.
- [x] Add the SP mouse Force wheel with a center dead zone, stable sector selection, and explicit select/cancel states.
- [ ] Check Force wheel icon readability and mouse feel during human play.
- [ ] Separate settings definitions, validation, Apply/Discard, and restart rules from presentation.
- [ ] Migrate settings screens, then main and pause menus.
- [ ] Migrate HUD components individually. Disable the old drawing path for each replaced component.
- [ ] Migrate character creation, Force allocation, mission selection, and datapad screens after their game bindings are covered.
- [ ] Retire the legacy framework only after its remaining responsibilities have replacements.

## Dependency Policy

Keep CMake. Do not introduce Conan or vcpkg for this initial dependency set.
Leave existing SDL, OpenGL, and other established dependency choices unchanged.

- [x] Pin RmlUi and FreeType source versions. Record release, exact revision or archive URL, SHA-256, build options, and local patches.
- [x] Use CMake FetchContent for the new dependencies. Do not track a moving branch or a latest-release URL.
- [x] Keep downloads and build output in an ignored cache. Support explicitly supplied local sources for offline builds.
- [ ] Retain source archives used for published builds. A checksum does not guarantee future download availability.
- [x] Build and statically link the new UI libraries into the client-side integration. Do not require matching UI shared libraries on the desktop.
- [x] Select FreeType features explicitly. Do not silently use optional libraries found on one build machine.
- [x] Disable unused Lua, Lottie, sample, profiling, and other optional components.
- [ ] Add a pinned SVG dependency only if selected artwork needs it. Geometry does not require an SVG loader.
- [ ] Version fonts, icons, layouts, and styles with their licenses and provenance. Do not rely on desktop-installed fonts.
- [x] Replace global language-standard flags with target-scoped requirements. RmlUi 6.3 requires C++17.
- [x] Raise the CMake minimum to match the integration actually used.
- [ ] Keep all builds, including dependency builds, at one job.

Static UI libraries do not make the entire application static. Existing platform
libraries and the graphics driver remain runtime dependencies. Fully reproducible
binaries also require a controlled compiler and system-library environment.

## Packaging and Tests

- [ ] Include UI assets, third-party notices, dependency revisions, hashes, and build options in each package.
- [ ] Keep the existing desktop pull-and-launch workflow. Do not install dependencies at game startup.
- [ ] Treat dependency upgrades as explicit changes. Run regression tests before publication.
- [ ] Automate input ownership, focus traversal, binding persistence, setting rollback, clipping, scale, aspect ratio, and restart tests.
- [ ] Test slow-time ownership and all wheel exit paths. Selection must not activate a power or leave movement/attack input held.
- [ ] Keep human checks for visual quality and interaction feel separate from automated correctness checks.

## References

- [RmlUi integration](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integrating.html)
- [RmlUi 6.3 build options](https://github.com/mikke89/RmlUi/blob/6.3/CMakeLists.txt)
- [CMake FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)
