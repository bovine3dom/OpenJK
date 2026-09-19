# Jedi Outcast reference source

This directory contains legacy JK2 single-player source. Use it as a reference when you port JO behavior to the active JA engine in `../code/`. Both JA and imported JO content run on the JA engine. Do not apply product changes here unless the user explicitly requests standalone JK2 work.

The legacy build remains available for reference and validation. Enable `BuildJK2SPGame` to build the game library. Enable `BuildJK2SPEngine` and `BuildJK2SPRdVanilla` to build the engine and renderer.

The build produces `openjo_sp.ARCH`, `rdjosp-vanilla_ARCH`, and `jospgameARCH`, with platform-specific file extensions.
