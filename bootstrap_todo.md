# Game Data Bootstrap and Launcher TODO

Research date: 2026-09-17.

## Decision

Build a small `openjk-launcher` executable. Make it the normal desktop entry
point for the single-player package. Keep `openjk_sp` as the game executable and
as a supported direct entry point for command-line users and tests.

The launcher must:

- Start without JA or JO data.
- Find installed JA and JO data in known locations.
- Let the user select a folder when detection does not find the data.
- Validate data before it saves a location or starts the game.
- Show separate **Play** and **New game** actions for Jedi Academy and Jedi
  Outcast.
- Keep the JA and JO profiles separate.
- Build the local JO overlay before it starts JO.
- Provide **Game files** and **Search again** actions after first-run setup.

Use RmlUi for the launcher window. Use SDL2 for the window and input. Use a
native folder dialog for folder selection.

Do not rebuild the in-game main menu as the first bootstrap implementation.
The current RmlUi integration starts too late for a data-free first run:

- `Com_Init()` calls `FS_InitFilesystem()` before client and renderer startup in
  `code/qcommon/common.cpp`.
- `FS_InitFilesystem()` stops when it cannot read `default.cfg` in
  `code/qcommon/files.cpp`.
- `CL_RmlUiInit()` runs after renderer registration in
  `code/client/cl_rmlui.cpp`.
- The current RmlUi file and render interfaces depend on `FS_ReadFile()` and
  the game renderer.
- `fs_cdpath`, `fs_basepath`, `fs_homepath`, and `com_outcast` are startup
  settings. A process restart is safer than a live filesystem change.

A data-free in-engine menu would need a second filesystem, a special renderer
startup path, and a partial game loop. A companion launcher has a smaller
failure area and gives the game a clean startup configuration.

## User Flow

### Normal Start

1. Load saved paths.
2. Validate saved paths in the background.
3. Search known install locations for missing games.
4. Show one card for JA and one card for JO.
5. Enable actions only when their requirements are ready.

The JA card has these actions:

- **Play Jedi Academy**: start the JA main menu.
- **New game**: start `yavin1`.
- **Locate files**: replace the saved JA location.

The JO card has these actions:

- **Play Jedi Outcast**: prepare the JO overlay and start the JO main menu.
- **New game**: prepare the JO overlay and start `kejim_post`.
- **Locate files**: replace the saved JO location.

JO needs both a valid JA installation and a valid JO installation. If JO is
present but JA is not present, show this requirement on the JO card. Do not
present JO as ready.

### First Run

- If one valid location is found for a game, select it and show the full path.
- If more than one valid location is found, show all valid locations. Include
  the source, such as Steam or GOG. Ask the user to select one.
- If no valid location is found, show **Locate files** immediately.
- Do not run a full-disk search.
- Do not block JA play when only JO is missing.
- Do not save an invalid selection.

The folder dialog title must name the game and ask for its installation or
`GameData` folder. The validator must also accept a `base` folder or a macOS game
app. This keeps the result recoverable when a user selects a nearby folder.

### Validation Errors

Keep the setup screen open after an error. Show one of these states:

| State | UI response |
| --- | --- |
| Ready | Show the game name and normalized path. Enable play actions. |
| Incomplete | List the missing PK3 files. Show **Choose another folder** and **Check again**. |
| Wrong game | State which game was found. Keep both paths unchanged. |
| Unsupported data | State that the files are not a known supported profile. Offer a diagnostic report. |
| Unreadable | State that OpenJK cannot read the folder. Mention sandbox access when applicable. |
| Damaged archive | Name the archive that cannot be opened. Suggest the store's verify or repair action. |
| Dialog unavailable | Show a path text field and explain how to enable a Linux desktop portal. |

Treat folder-dialog cancellation as a normal return to the setup screen.

### JO Import

On the first JO start, show:

- The import destination.
- Required free space.
- Current file or stage.
- Progress when the total work is known.
- A safe cancel action before final replacement.

Keep the previous valid overlay until the new overlay is complete. On failure,
show the error and a **Retry** action. Do not start JO with a partial overlay.

## Launcher Architecture

### Executables

- Add an `openjk-launcher` target for Windows, macOS, and Linux.
- Install it with the JA single-player package.
- Point desktop shortcuts and application entries to the launcher.
- Keep `openjk_sp` available for scripts, tests, and advanced command-line use.
- Start `openjk_sp` as a child process. Do not pass the command through a shell.

Keep the launcher open during JO import because it owns the progress UI. Exit
the launcher after a successful game-process start.

### RmlUi Startup

Use the existing pinned RmlUi 6.3 and FreeType 2.13.3 dependencies. Do not use
the engine `ReticleRenderer` or `GameFiles` classes in the launcher.

RmlUi 6.3 supplies standalone SDL platform and OpenGL backends in its `Backends`
directory. Build a small launcher backend from those sources. The upstream
backends are reference code and are designed to be copied or adapted.

Initial backend plan:

- Use the RmlUi SDL platform adapter for SDL2 input and window events.
- Spike the SDL/OpenGL 2 and SDL/OpenGL 3 render adapters on all release
  platforms. Select one before the UI work starts.
- Load the RML, RCSS, and IBM Plex fonts from launcher-owned files or embedded
  byte arrays. Do not read them through the game VFS.
- Use CSS shapes and text for the first version. This avoids a new image loader.
- Adapt the selected backend so it does not include the sample `SDL_image`
  texture loader when the launcher has no image resources.
- Do not use RmlUi's SDL renderer adapter with the current bundled SDL 2.0.12.
  That adapter calls `SDL_RenderGeometry()`, which needs SDL 2.0.18 or later.

Do not migrate the full project to SDL3 only to get a folder dialog. SDL3 has
`SDL_ShowOpenFolderDialog()`, but the project uses SDL2 and a full migration has
much more risk than one small dialog dependency.

### Folder Dialog

Use [Native File Dialog Extended](https://github.com/btzy/nativefiledialog-extended)
(NFDe) unless the backend spike finds a release blocker.

NFDe provides:

- UTF-8 paths on Windows, macOS, and Linux.
- A folder picker.
- SDL2 parent-window integration.
- Native Windows and macOS dialogs.
- GTK or XDG desktop portal backends on Linux.
- A Zlib license.

Build the Linux release with `NFD_PORTAL=ON`. The portal permits folder access
from a Flatpak sandbox and uses the desktop's preferred dialog. Keep a manual
path field because the portal can be absent or older than FileChooser version 3.

Call NFDe on the UI thread. Initialize it after SDL and stop it before SDL. Pass
the SDL parent window handle. Distinguish `NFD_CANCEL` from `NFD_ERROR`.

### Shared Core

Keep detection, path normalization, validation, configuration, JO import, and
launch argument construction independent of RmlUi. The launcher UI must only
present state and request actions.

Use these logical modules:

- Candidate providers: saved path, portable path, Steam, Windows registry,
  macOS applications, and optional Wine locations.
- Path normalizer: convert a selected or detected path to the directory that
  contains `base`.
- Asset validator: identify JA or JO and return a detailed result.
- Bootstrap configuration: load and save selected paths.
- Campaign preparation: check or build the JO overlay.
- Process launcher: start the game with an argument vector.

Run bounded discovery and archive validation on a worker thread. Send immutable
results to the UI thread. Do not call RmlUi from the worker thread.

## Configuration

Store bootstrap configuration outside the game VFS. Put it in the normal OpenJK
profile root:

- Linux: `$XDG_DATA_HOME/openjk`, or `$HOME/.local/share/openjk`.
- macOS: `$HOME/Library/Application Support/OpenJK`.
- Windows: `Documents/My Games/OpenJK`.
- Portable package: a documented writable folder beside the package.

Use a small UTF-8 `bootstrap.ini` file:

```ini
version=1
ja_path=/path/to/Jedi Academy/GameData
jo_path=/path/to/Jedi Outcast/GameData
last_campaign=ja
```

Split each line at its first `=`. Reject NUL, CR, and LF in a path. An `=` in a
path remains valid. On Windows, convert between UTF-16 OS paths and UTF-8 file
values without a lossy ANSI conversion.

Write a temporary file, flush it, and replace the previous file. Set user-only
permissions where the platform supports them. Save only validated paths.

If a future sandboxed macOS package is required, save a security-scoped bookmark
with the path. A plain path does not retain sandbox permission. This is not
needed for a normal, non-sandboxed application bundle.

## Candidate Discovery

Always use this priority:

1. Explicit command-line path for tests and package integrations.
2. Saved path.
3. Valid data beside the launcher or package.
4. Store and operating-system metadata.
5. Known default folders.
6. User-selected folder.

Collect all candidates before selection. Do not stop after a stale manifest or
registry entry. Normalize and validate every candidate. Deduplicate paths by
filesystem identity where possible. Keep the original user-visible path for
display and sandbox access.

### Steam

Use Steam app ID [6020](https://store.steampowered.com/app/6020/) for JA and
[6030](https://store.steampowered.com/app/6030/) for JO.

Find Steam roots at these locations:

| Platform | Root candidates |
| --- | --- |
| Windows | `HKCU\Software\Valve\Steam` value `SteamPath`; `HKLM\Software\Valve\Steam` value `InstallPath`; known Program Files folders as fallbacks. |
| macOS | `~/Library/Application Support/Steam`. |
| Linux | `~/.local/share/Steam`, `~/.steam/steam`, `~/.steam/root`, and `~/.steam/debian-installation`. |
| Linux Flatpak Steam | `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`, when visible. |
| Linux Snap Steam | `~/snap/steam/common/.local/share/Steam` and `~/.snap/data/steam/common/.local/share/Steam`, when visible. |

For each root:

1. Parse `steamapps/libraryfolders.vdf`.
2. Support both the old string form and the new object form with a `path` key.
3. Include the primary Steam root even when the VDF file is absent or damaged.
4. Parse `steamapps/appmanifest_6020.acf` and
   `steamapps/appmanifest_6030.acf` in every library.
5. Read `AppState/installdir` case-insensitively.
6. Resolve the install under `steamapps/common`.
7. Use `Jedi Academy` and `Jedi Outcast` only as fallback folder names.

Use a bounded Valve KeyValues parser. Do not parse VDF with a regular
expression. Limit input size and nesting depth. Support quoted strings, escaped
backslashes, comments, unknown keys, and malformed-file recovery.

Do not require Steamworks. Valve documents that `GetAppInstallDir()` can return
a default path when an app is not installed. Local manifests plus asset
validation give a deterministic result without Steam initialization.

### Windows Retail and GOG

Read both 32-bit and 64-bit registry views. Do not address `WOW6432Node`
directly. Use `KEY_WOW64_32KEY` and `KEY_WOW64_64KEY`.

Check these retail keys under `HKLM`:

```text
SOFTWARE\LucasArts\Star Wars Jedi Knight Jedi Academy\1.0
SOFTWARE\LucasArts Entertainment Company LLC\Star Wars JK II Jedi Outcast\1.0
```

Accept both `Install Path` and `InstallPath` values.

For GOG, enumerate `HKLM\Software\GOG.com\Games` in both registry views. Read
the `path` value from each child. Treat known product IDs as hints, not as the
only source of truth. Inspect `goggame-*.info` when present, then validate the
assets. Add bounded fallback checks below `C:\GOG Games` and the GOG Galaxy
games folder.

Use Windows known-folder APIs for Program Files and Documents. Do not assume
that Windows is on drive `C:`.

### macOS

Check Steam manifests first. Normalize these known bundle layouts:

```text
Jedi Academy/SWJKJA.app/Contents/base
/Applications/Star Wars Jedi Knight: Jedi Academy.app/Contents/base
Jedi Outcast/Jedi Knight II.app/Contents/base
/Applications/Jedi Knight II.app/Contents/base
```

Also check the equivalent paths below `~/Applications`. Let the user select a
renamed or moved `.app` bundle. Inspect only known relative paths below a
selected bundle, such as `Contents/base` and `Contents/Resources/GameData/base`.

Do not assume one bundle name covers every Mac App Store, Steam, retail, and GOG
release. Asset validation is authoritative.

### Linux and Wine

Native Steam and Proton use the normal Steam install under
`steamapps/common`. The launcher does not need to inspect a Proton prefix for a
normal Steam installation.

Add Wine as a second-phase provider:

- Check an explicit `WINEPREFIX`.
- Check `~/.wine`.
- Read the retail and GOG registry data from those known prefixes.
- Do not follow `dosdevices/z:` or scan the full host filesystem.
- Add separate providers for Lutris, Bottles, Heroic, and CrossOver only when
  fixtures for their current metadata formats are available.

### Sandboxes

- A launcher in Flatpak cannot inspect every host or Steam path. Use portal
  selection as the reliable fallback.
- A strict Snap cannot read all hidden folders or another Snap's data. Declare
  required package interfaces and keep folder selection available.
- Preserve the path returned by the portal. Do not replace it with a host path
  that the sandbox cannot access.
- Ask the user to select `GameData` or the app bundle. A grant for only a
  `base` folder can conflict with the engine's current parent-plus-`base`
  filesystem layout.

Epic Games Store support is not in the first scope. This research did not
confirm an Epic release of either game. Do not add unverified product IDs.

## Path Normalization

For each candidate, check only these bounded layouts:

```text
<selected>/base
<selected>/GameData/base
<selected>                         when selected is base
<selected>/*.app/Contents/base
<selected>.app/Contents/base
<selected>.app/Contents/Resources/GameData/base
```

Return the data root whose direct child is `base`. Record the asset directory
separately. Do not recursively search an arbitrary selected tree.

Handle symlinks and junctions, but keep the selected path for display. Detect
path overlap between the package, original data, profile, import temporary
directory, and import output. Never write to an original game installation.

## Asset Validation

Use two validation levels.

### Fast Discovery Check

Require readable, nonempty files:

| Game | Required patched PC files |
| --- | --- |
| JA | `base/assets0.pk3`, `assets1.pk3`, `assets2.pk3`, `assets3.pk3` |
| JO | `base/assets0.pk3`, `assets1.pk3`, `assets2.pk3`, `assets5.pk3` |

These file names are not enough to identify a game. JA and JO share the first
three names.

### Readiness Check

- Open each required PK3 as a ZIP archive.
- Read the central directory without extracting all content.
- Identify the game from several distinctive entries.
- For JO, include `maps/kejim_post.bsp`, `maps/kejim_base.bsp`, and
  `ext_data/npcs.cfg`. The current importer already requires these entries.
- For JA, define a similar manifest that includes the opening map and JA-only
  gameplay data. Derive it from real supported editions before release.
- Confirm the humanoid animation data that the JO importer reads from both
  installations.
- Reject a truncated or malformed ZIP before launch.
- Do not require the original executable.
- Do not use `default.cfg` as the only retail-data test.

Create an edition manifest from legally obtained Steam, GOG, retail, and Mac
fixtures. Use sizes or checksums only to identify known editions or damage. Do
not reject a valid regional edition only because its complete archive hash is
new. Report an unknown profile separately and capture enough metadata for a
maintainer to add it without collecting proprietary file content.

## JO Import Blocker

The current JO path is not ready for normal cross-platform users because
`scripts/import-jo.py` requires Python 3.9 or later. Windows and macOS users do
not reliably have Python. The current script also reports only text output.

Resolve this before a public JO package. Port the importer to a small native
library that the launcher and tests can call. Do not ask normal users to install
Python. Keep the Python implementation as a test oracle until archive-content
and behavior checks pass.

The native import API must provide:

- JA root, JO root, and output profile inputs.
- A progress callback.
- A cancellation check.
- Structured error codes and a detailed log.
- A required-space estimate.
- Temporary output followed by atomic replacement.
- A cache signature for importer version and source archive metadata.

Keep the current profile contract:

```text
<JA profile>/OpenJK/...                 JA settings and saves
<JA profile>/campaigns/jo/OpenJK/...    JO settings, saves, and overlay
```

Allow at least 1 GB of free work space, as documented by the current importer.
Recheck both source installations before reuse of a cached overlay.

## Starting the Game

Build a platform-native argument vector. Never concatenate a shell command.
Preserve this current filesystem contract:

```text
+set fs_basepath <package>
+set fs_cdpath <JA GameData>
+set fs_homepath <selected profile>
+set fs_game OpenJK
+set com_outcast 0|1
```

For **New game**, add `+map yavin1` for JA or `+map kejim_post` for JO.

Use `CreateProcessW()` and a tested Windows argument encoder on Windows. Use
`posix_spawn()` with an argument array on macOS and Linux. Test spaces, Unicode,
quotes, plus signs, and long paths. The current engine flattens arguments into a
fixed `MAX_STRING_CHARS` buffer, so add a regression test and remove that limit
if valid launcher paths can exceed it.

Use a cross-platform single-instance lock for import and package updates. Do not
rely on Bash, `flock`, `/proc/self/fd`, `realpath`, or GNU command behavior in
the release launcher.

## Implementation Plan

### Phase 0: Resolve Release Decisions

- [x] Confirm that the public product is the shared JA executable with imported
  JO, not the separate `openjo_sp` executable.
- [x] Select the launcher binary name, application name, icon, and package
  layout on each platform.
- [ ] Select the RmlUi SDL/OpenGL backend after a three-platform spike.
- [x] Pin an NFDe release or commit and record its source hash and license.
- [ ] Define minimum supported Windows, macOS, and Linux environments.

### Phase 1: Build the Headless Bootstrap Core

- [ ] Add game, candidate-source, validation-state, and validation-result types.
- [ ] Add bounded path normalization.
- [x] Add JA and JO fast validation.
- [x] Add ZIP central-directory validation and game identification.
- [x] Add explicit-path and saved-path providers.
- [ ] Add adjacent portable-data discovery.
- [ ] Add a bounded Valve KeyValues parser.
- [ ] Add Steam root, library, and manifest providers for all three platforms.
- [ ] Add Windows retail and GOG registry providers.
- [ ] Add macOS application-bundle providers.
- [x] Add atomic `bootstrap.ini` persistence.
- [x] Add a diagnostic command, such as
  `openjk-launcher --headless-check --ja-path PATH --jo-path PATH`.

### Phase 2: Add the Standalone RmlUi Launcher

- [x] Add the CMake target and launcher-owned file interface.
- [x] Make the RmlUi dependency setup shared instead of game-client-specific.
- [x] Add the SDL2 window, render backend, event loop, and high-DPI handling.
- [x] Load only redistributable launcher fonts and UI resources.
- [ ] Add keyboard, mouse, text, and gamepad navigation.
- [ ] Add the searching, ready, ambiguous, missing, invalid, and error states.
- [x] Add JA and JO cards with **Play**, **New game**, and **Locate files**.
- [ ] Add **Search again**, **Check again**, and **Game files** actions.
- [x] Add NFDe and its parent-window integration.
- [x] Add a manual UTF-8 path field for dialog failure and automated tests.
- [x] Keep discovery work off the UI thread.

### Phase 3: Launch JA End to End

- [x] Construct the exact current JA profile and filesystem arguments.
- [x] Add Windows, macOS, and Linux process launch implementations.
- [x] Report a missing engine binary or failed child start in the launcher.
- [x] Update desktop shortcuts and application entries to use the launcher.
- [x] Keep direct `openjk_sp` startup working.
- [ ] Retire `launch-sp.sh` from the public package after feature parity. Keep it
  as a development tool if it remains useful.

### Phase 4: Make JO Self-Contained

- [x] Port or package the JO importer.
- [ ] Add import progress, cancellation, free-space checks, logs, and retry.
- [x] Keep import output outside both retail installations.
- [x] Keep the previous valid overlay until replacement succeeds.
- [x] Match the Python importer's output and cache behavior in tests.
- [x] Start JO with `com_outcast=1` and the isolated JO profile.
- [x] Revalidate sources before each import or cached-overlay reuse.

### Phase 5: Package Each Platform

- [ ] Windows: install the launcher, NFDe, SDL2, FreeType, RmlUi notices, engine
  modules, and launcher shortcut. Test a non-admin install and Unicode paths.
- [ ] macOS: put the launcher and helper game executable in one signed app
  bundle. Test Intel and Apple Silicon. Test Steam and app-bundle data.
- [ ] Linux: provide a tar package and `.desktop` file first. Add AppImage or
  Flatpak only with a tested portal and filesystem-permission plan.
- [x] Make the package relocatable. Resolve resources from the executable or
  bundle, not the current working directory.
- [ ] Give portable mode an explicit marker and a clear writable-data rule.
- [ ] Include all third-party licenses and corresponding source obligations.

### Phase 6: Public-Package Audit

- [ ] Remove all developer-machine paths and update-only assumptions.
- [ ] Do not ship retail game data or files derived from retail game data.
- [x] Audit `scripts/build-sp.sh`: it currently creates `OpenJK/sky-hd.pk3` from
  local retail sky images. Generate that file on the user's machine or omit it
  from a public package until its distribution rights are clear.
- [x] Ensure that JO overlays are generated locally and are never part of a
  downloadable package.
- [x] Add a clear statement that the user must own JA and JO for the respective
  campaigns.
- [x] Document where paths, settings, saves, imports, and logs are stored.

## Test Plan

### Automated Core Tests

- [ ] Parse old and new `libraryfolders.vdf` forms.
- [ ] Handle comments, escaped backslashes, unknown keys, bad UTF-8, excessive
  nesting, and truncated VDF input.
- [ ] Find app manifests in the primary library and additional libraries.
- [ ] Continue after stale manifests and inaccessible library roots.
- [ ] Normalize install root, `GameData`, `base`, and macOS bundle selections.
- [ ] Reject unrelated folders, wrong-game data, missing PK3s, empty files,
  malformed ZIPs, and missing sentinel entries.
- [x] Test JA and JO synthetic PK3 fixtures without proprietary content.
- [ ] Test paths with spaces, `=`, quotes, plus signs, non-ASCII characters,
  symlinks, junctions, and platform path limits.
- [ ] Test interrupted configuration writes and import replacement.
- [x] Test profile and source overlap rejection.
- [ ] Test exact child arguments for JA menu, JA new game, JO menu, and JO new
  game.
- [ ] Test folder-dialog cancel and error separately.
- [ ] Test discovery on a worker thread and UI-safe result delivery.

### Graphical Tests

- [x] Run the launcher headlessly where possible.
- [ ] Capture missing, ambiguous, ready, invalid, and import-failure screens.
- [ ] Test keyboard-only and gamepad-only setup.
- [ ] Test 100%, 150%, and 200% display scale.
- [ ] Test a small laptop display and an ultrawide display.
- [ ] Test resize, minimize, focus loss, and folder-dialog parenting.

### Manual Platform Matrix

| Platform | Required checks |
| --- | --- |
| Windows 10 and 11 | Steam default library, Steam additional library, GOG, retail registry, copied files, non-admin user, Unicode path. |
| macOS Intel and Apple Silicon | Steam, `/Applications` bundle, `~/Applications` bundle, moved or renamed bundle, signed package. |
| Linux X11 and Wayland | Native Steam, additional library, copied files, portal picker, missing portal, read-only source. |
| Linux sandbox | Flatpak Steam visibility, portal-selected external library, saved access after restart. |
| Steam Deck | Game mode launch, desktop mode setup, on-screen keyboard, controller-only navigation. |

## Release Acceptance

- [ ] A new user can unpack or install the package and start the launcher by
  using the normal desktop action.
- [ ] JA is ready without manual path entry for a default Steam or GOG install.
- [ ] JO is ready without manual path entry when both default installations are
  present.
- [ ] A copied installation can be selected with a native folder dialog.
- [ ] A bad selection gives a specific, recoverable error.
- [ ] No supported launch path needs Bash, Python, a terminal, or an edit to a
  configuration file.
- [x] JA and JO settings and saves remain separate.
- [x] An interrupted JO import cannot damage the last valid import.
- [x] The public package contains no proprietary or locally derived game data.
- [ ] The launcher and game start on supported Windows, macOS, and Linux test
  systems.

## Research Findings

The projects below use the same broad pattern: detect known installs, validate
the data, and provide a browse or retry path.

- [OpenRCT2 installation guide](https://docs.openrct2.io/en/latest/installing/installing-on-windows.html):
  first launch checks known locations and opens a directory dialog if discovery
  fails.
- [OpenRCT2 configuration source](https://github.com/OpenRCT2/OpenRCT2/blob/develop/src/openrct2/config/Config.cpp):
  checks platform and Steam locations, validates candidates, stores the path,
  and loops back after an invalid selection.
- [OpenMW installation wizard guide](https://openmw.readthedocs.io/en/stable/manuals/installation/install-game-files.html):
  shows detected installs and lets the user browse when none is suitable.
- [OpenMW existing-installation source](https://github.com/OpenMW/openmw/blob/master/apps/wizard/existinginstallationpage.cpp):
  checks related files, reports a specific missing file, and does not advance
  until an installation is valid.
- [ScummVM launcher guide](https://docs.scummvm.org/en/latest/use_scummvm/add_play_games.html):
  uses a persistent game list, supports a native or internal browser, and asks
  the user to resolve ambiguous versions.
- [OpenRA source installation flow](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/Widgets/Logic/Installation/InstallFromSourceLogic.cs):
  separates source detection, progress, required content, failure, back, and
  retry states.
- [GZDoom IWAD discovery](https://github.com/ZDoom/gzdoom/blob/master/src/d_iwad.cpp):
  separates search locations from archive-content identification and dependency
  checks. Its fatal no-data path is a useful example of what this launcher
  should avoid.
- [RmlUi integration guide](https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integrating.html):
  documents standalone platform and renderer backends. RmlUi does not provide a
  native folder-dialog API.
- [RmlUi 6.3 SDL backend sources](https://github.com/mikke89/RmlUi/tree/6.3/Backends):
  provide SDL2 platform, OpenGL, and SDL renderer reference implementations.
- [SDL2 filesystem API](https://wiki.libsdl.org/SDL2/CategoryFilesystem): SDL2
  provides base and preference paths, but no file or folder dialog.
- [SDL2 `SDL_RenderGeometry`](https://wiki.libsdl.org/SDL2/SDL_RenderGeometry):
  the function used by RmlUi's SDL renderer backend is available from SDL
  2.0.18. The repository's bundled SDL headers report 2.0.12.
- [Native File Dialog Extended](https://github.com/btzy/nativefiledialog-extended):
  supplies UTF-8 native folder dialogs, SDL2 parent integration, and a Linux
  portal backend.
- [XDG FileChooser portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.FileChooser.html):
  directory selection starts with interface version 3, and selected document
  paths can stay accessible across sessions.
- [Protontricks Steam discovery](https://github.com/Matoking/protontricks/blob/master/src/protontricks/steam.py):
  handles native, Flatpak, and Snap Steam roots, both Steam library VDF forms,
  additional libraries, and manifests.
- [Valve `ISteamApps`](https://partner.steamgames.com/doc/api/ISteamApps#GetAppInstallDir):
  confirms that `GetAppInstallDir()` is not proof that an app is installed.
- [JK2MV Windows discovery](https://github.com/mvdevs/jk2mv/blob/master/src/sys/sys_win32.cpp)
  and [macOS discovery](https://github.com/mvdevs/jk2mv/blob/master/src/sys/sys_unix.cpp):
  provide tested retail registry keys and JA or JO app-bundle layouts.
- [Microsoft registry-view guidance](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view):
  requires two-pass enumeration for both registry views and advises applications
  not to use `WOW6432Node` directly.
- [OpenJK player instructions](https://github.com/JACoders/OpenJK/blob/master/README.md):
  confirm the Steam JA `GameData` layout, Steam app ID 6020, and the Mac App
  Store bundle layout.

## Open Questions

- What public product name should distinguish this fork from upstream OpenJK?
- Must the first public build support portable mode, Flatpak, or an app-store
  sandbox, or can these follow the native packages?
- Which original retail and native macOS editions are available for validation
  fixtures?
- Is the public JO milestone blocked until the full campaign has a manual
  completion test, as noted in `docs/jo-campaign.md`?
