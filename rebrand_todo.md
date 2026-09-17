# OpenJedvibe Rebrand Checklist

## Scope

OpenJedvibe is a single-player fork based on OpenJK. The first rebrand changes
the public product identity. It does not change save, profile, module, or mod
compatibility identifiers.

## Compatibility Rules

- [x] Keep the existing OpenJK profile roots on Windows, macOS, and Linux.
- [x] Keep the `OpenJK/` runtime directory and `+set fs_game OpenJK`.
- [x] Keep `openjk_sp.cfg` and the existing save format.
- [x] Keep game and renderer module names, including `jagame` and `rdsp-*`.
- [x] Keep OpenJK API symbols and internal CMake target names where practical.
- [x] Keep `.openjk-managed` so existing rsync installations stay managed.
- [x] Keep OpenJK and JACoders copyright, license, contributor, and upstream
  attribution.

## Public Identity

- [x] Change launcher text, window titles, diagnostics, and desktop labels to
  OpenJedvibe.
- [x] Change the current SP engine title and version text to OpenJedvibe.
- [x] Change Windows product metadata to OpenJedvibe without changing historical
  copyright fields.
- [x] Change the macOS app name and bundle identifier.
- [x] Change the Linux desktop filename, display name, and executable command.

## Distributed Files

- [x] Rename the SP engine to `openjedvibe_sp.ARCH`.
- [x] Rename the launcher to `openjedvibe-launcher`.
- [x] Rename the native JO importer to `openjedvibe-import-jo`.
- [x] Update launcher engine discovery and all package validation scripts.
- [x] Update the rsync updater and its synthetic package tests.
- [x] Rename release archives and CI artifacts from OpenJK to OpenJedvibe.

## Packages

- [x] Change CPack and NSIS product names, install directory, vendor, and SP
  shortcut.
- [x] Keep the `JediAcademy` package subdirectory because it identifies the base
  game, not the fork.
- [x] Keep retail files and generated JO imports outside public packages.
- [x] Confirm that the public package does not require Python.

## Documentation

- [x] Introduce OpenJedvibe as a fork based on OpenJK.
- [x] Update launcher, package, test, and distribution commands.
- [x] Keep `OpenJK` in instructions that describe retained compatibility paths.
- [x] Keep upstream OpenJK links and attribution accurate.

## Verification

- [x] Configure and build the renamed launcher, importer, engine, and modules.
- [x] Run importer, launcher, updater, and package tests.
- [x] Run both Linux renderer smoke tests from the renamed package.
- [x] Confirm that existing OpenJK profiles and saves remain in use.
- [x] Confirm that JA and JO still use separate profile subdirectories.
- [x] Configure the Windows and macOS package layouts.
- [ ] Test native Windows and macOS packages on those systems.

## Deferred Decisions

- [ ] Replace or adapt icon artwork for OpenJedvibe.
- [ ] Rename the GitHub repository and update fork-owned URLs and badges.
- [ ] Select the final publisher name and support URL.
- [ ] Decide whether to rebrand dormant multiplayer and standalone OpenJO builds.
- [ ] Consider a separate OpenJedvibe profile root only with an explicit,
  non-destructive migration for existing saves and settings.
