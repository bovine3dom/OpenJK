# RmlUi Dependencies

## Scope

`BuildRmlUi` is ON by default when `BuildSPEngine` is ON. It adds the RmlUi
reticle to the Jedi Academy single-player client only. Set `BuildRmlUi=OFF`
to omit the integration and its dependencies. Jedi Outcast and multiplayer
targets do not link these libraries.

The MVP uses geometry-only RML embedded in `code/client/cl_rmlui.cpp`.
It does not need font files, textures, or a separate asset package.
FreeType supplies the default RmlUi Core font engine.

## Source Archives

The build uses these unmodified release archives. There are no local patches.
CMake checks each downloaded archive against its SHA-256 value.

| Library | Release | Archive URL | SHA-256 |
| --- | --- | --- | --- |
| RmlUi | 6.3 | <https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.3> | `d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede` |
| FreeType | 2.13.3 | <https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz> | `0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289` |

Upstream projects: [RmlUi](https://github.com/mikke89/RmlUi) and
[FreeType](https://freetype.org/).

## Build Settings

CMake 3.28 or later is required. OpenJK targets default to C++11 without
compiler extensions. RmlUi Core requires C++17 and supplies that requirement
to the SP client through `RmlUi::Core`.

Both libraries are static. RmlUi uses the local `Freetype::Freetype` target;
it does not search for a system FreeType installation.

FreeType settings are explicit: `FT_DISABLE_ZLIB`, `FT_DISABLE_BZIP2`,
`FT_DISABLE_PNG`, `FT_DISABLE_HARFBUZZ`, and `FT_DISABLE_BROTLI` are ON.
Their `FT_REQUIRE_*` settings and `FT_ENABLE_ERROR_STRINGS` are OFF.
FreeType keeps its internal gzip/zlib implementation. It does not link the
optional system libraries. These settings do not change OpenJK's existing
SDL, PNG, zlib, or other dependency settings.

RmlUi samples, tests, shell, Lua bindings, Lottie, SVG, HarfBuzz sample,
Tracy, third-party containers, precompiled headers, custom configuration,
and upstream compiler options are OFF. Sample font downloads are OFF.
Only Core is linked. The upstream Debugger target has no disable option;
it is excluded from the default build with the other unused targets.
Dependency SDK files are excluded from installation.

## Cache And Offline Use

Use an ignored build directory, for example `build/sp`. FetchContent stores
downloads, extracted sources, and dependency build files in its `cache`
subdirectory by default. Each build directory has a separate cache.
`FETCHCONTENT_BASE_DIR` can select another cache directory.
Do not commit downloaded sources or archives.

For an offline configure, first check the archive hashes above and extract
both archives. Supply the extracted source directories with the standard
FetchContent overrides:

```sh
cmake -S . -B build/sp \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE=/absolute/path/to/freetype-2.13.3 \
  -DFETCHCONTENT_SOURCE_DIR_RMLUI=/absolute/path/to/RmlUi-6.3
```

CMake does not check archive hashes for source-directory overrides. The
operator must check those sources before use. Do not use
`FETCHCONTENT_FULLY_DISCONNECTED=ON` for an empty cache without source overrides.

## Licenses And Packages

RmlUi uses the MIT license. FreeType offers the FreeType License (FTL) or
GPL version 2. This GPL version 2 project can use the GPL version 2 option.
Parts of this software are copyright (C) 2024 The FreeType Project
(https://freetype.org/). All rights reserved.

CMake installs this document in `JediAcademy/rmlui-dependencies.md` with the
`JKASPClient` component. It installs the RmlUi license in
`JediAcademy/licenses/rmlui/`. It installs FreeType's `LICENSE.TXT`,
`FTL.TXT`, and `GPLv2.TXT` in `JediAcademy/licenses/freetype/`.
That directory also contains `zlib.h`, which includes the license for
FreeType's internal zlib code. The third-party RmlUi containers are disabled.
The Debugger and its font are not part of this package.

`scripts/build-sp.sh` includes these files through `cmake --install`.
Keep these notices with binary packages. For source distribution, also
supply the corresponding dependency sources under their license terms;
this document and its download URLs do not replace that requirement.
