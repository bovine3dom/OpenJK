# Incremental UI Plan

## Agreed Direction

- Use a minimal visual style that retains the Jedi Academy theme.
- Use crisp text, geometric panels, thin borders, restrained highlights, and consistent spacing.
- Support independent UI scale and aspect-correct layouts. A larger framebuffer alone does not make the UI sharp.
- Keep the existing scale controls. Do not add separate UI and text scale settings.
- Replace low-resolution artwork where needed. A UI library cannot recover missing image detail.
- The Force wheel slows game time while held. Release selects a power. The normal activation button uses it.
- Keep direct Force bindings. Cancel the wheel on focus loss, death, or a cinematic transition.
- Update wheel animation and input in real time. Restore the appropriate game speed on close, not a fixed timescale of 1.
- First MVP: replace the stretched reticle with an aspect-correct reticle. Keep collision-based movement and enemy/friendly identification.

## Reticle MVP Status

- [x] Set the default dot scale to 0.75.
- [x] Replace the bottom-right SP resource panel with contextual Force and ammo rings and a stance arc.
- [x] Replace the bottom-left panel with paired health and shield arcs. Retain a faint critical-health arc at 25% or below.
- [x] Bind V to a held status view with exact resource values, HUD panels, and ally bearings. Preserve an existing user binding.
- [ ] Check ring size, opacity, fade timing, and stance readability during human play.

- [x] Integrate RmlUi 6.3 into the Jedi Academy SP client with static dependencies.
- [x] Replace normal reticle artwork in vanilla and Rend2. Keep the existing collision and target-color logic.
- [x] Add `cg_rmluiReticle` for legacy fallback and `cg_rmluiReticleScale` for independent size control.
- [x] Destroy the RmlUi context before renderer shutdown and recreate it after registration.
- [x] Test 4:3 and 16:9 output, equal pixel dimensions, scale, hiding, fallback, and `vid_restart` on both renderers.
- [ ] Check collision movement, enemy/friendly colors, Force hints, vehicles, pickup animation, and reduced view size in gameplay.
- [ ] Check the result on hardware graphics drivers.

The integration supports geometry, rectangular clipping, generated font textures,
shared JA and JO Force and weapon selection screens, and the atmosphere editor.
The selection screens use native game shaders for original artwork. General
image-file loading is not supported. Settings, main, pause, character creation,
mission selection, and datapad screens still use the legacy framework. MP,
turret artwork, and the Force corona still use their existing paths.
See [MVP use and tests](docs/rmlui-reticle.md) and
[dependency records](docs/rmlui-dependencies.md).

## Migration

The first SP Force wheel uses a normal controls-menu binding. G is the initial
default when free. Middle mouse can be assigned in the menu. See
[Force wheel controls and tests](docs/force-wheel.md).

- [x] Keep movement active while the Force wheel is open. Block combat actions and camera turning.
- [x] Render the wheel label with bundled IBM Plex Mono through FreeType at native pixel size.
- [x] Highlight the current Force power in the dead zone and use the radial display for Q/E cycling without slow time.
- [x] Use a separate radial weapon display for scroll cycling without slow time.
- [x] Add an H-held weapon wheel with slow time and a controls-menu binding. Keep scroll cycling at normal time.
- [x] Refine wheel typography with a stronger font weight, a thin outline, and consistent spacing.
- [x] Start the gameplay text migration: objectives, captions, notifications, HUD labels, and numbers use Plex. Keep menus on their existing path.
- [x] Migrate datapad text to IBM Plex Sans SemiBold. Preserve colors and row heights, and measure proportional wrapping.
- [x] Use Plex Sans for mission-complete text and correct speaker portrait proportions.
- [ ] Check datapad content and navigation during campaign play, including long localized objectives and move descriptions.
- [ ] Review gameplay text sizes and line breaks during human play, including localized content.

RmlUi is integrated for the reticle, wheels, gameplay text, the atmosphere
editor, and Force and weapon selection. Keep existing screens available during
further migration.

- [x] Add a renderer-neutral drawing interface for vanilla and Rend2, with explicit clipping and resource lifetime.
- [x] Add filesystem, localization, font, and input adapters. Keep the existing event loop, cvar store, and binding store.
- [x] Give each migrated screen one UI owner. Transfer focus explicitly between old and new menus.
- [x] Test selection screens at standard, wide, and tall resolutions and across `vid_restart`.
- [ ] Test independent UI scales and keyboard focus traversal on the selection screens.
- [x] Add the SP mouse Force wheel with a center dead zone, stable sector selection, and explicit select/cancel states.
- [ ] Check Force wheel icon readability and mouse feel during human play.
- [ ] Separate settings definitions, validation, Apply/Discard, and restart rules from presentation.
- [ ] Migrate settings screens, then main and pause menus.
- [ ] Migrate HUD components individually. Disable the old drawing path for each replaced component.
- [x] Move JA and JO Force allocation and weapon selection to RmlUi. Keep the original artwork and display font. Use Plex for standard text.
- [ ] Migrate character creation, mission selection, and datapad screens after their game bindings are covered.
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
- [x] Version the current fonts, layouts, and styles with their licenses and provenance. Do not rely on desktop-installed fonts.
- [ ] Record licenses and provenance for each new icon or art asset added during later migrations.
- [x] Replace global language-standard flags with target-scoped requirements. RmlUi 6.3 requires C++17.
- [x] Raise the CMake minimum to match the integration actually used.
- [ ] Keep all builds, including dependency builds, at one job. Local SP builds comply; GitHub workflows still use automatic job counts.

Static UI libraries do not make the entire application static. Existing platform
libraries and the graphics driver remain runtime dependencies. Fully reproducible
binaries also require a controlled compiler and system-library environment.

## Packaging and Tests

- [x] Include UI assets, third-party notices, dependency revisions, hashes, and build options in each package.
- [x] Keep the existing desktop pull-and-launch workflow. Do not install dependencies at game startup.
- [ ] Treat dependency upgrades as explicit changes. Run regression tests before publication.
- [x] Automate wheel input ownership and binding persistence, reticle scale and aspect, and selection-screen aspect and restart tests.
- [ ] Add scissor-clipping and clipping-state restoration tests for both renderers.
- [ ] Add keyboard focus-traversal, independent UI-scale, and setting-rollback tests.
- [x] Test slow-time ownership and known wheel exit paths. Selection does not activate a power or leave movement or attack input held.
- [ ] Test controller input and any new wheel exit paths added during later screen migration.
- [x] Keep human checks for visual quality and interaction feel separate from automated correctness checks.

## References

- [RmlUi integration](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integrating.html)
- [RmlUi 6.3 build options](https://github.com/mikke89/RmlUi/blob/6.3/CMakeLists.txt)
- [CMake FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)
