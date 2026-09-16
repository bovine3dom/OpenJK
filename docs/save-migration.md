# Save Migration

The shared runtime accepts project save formats 1 through 4. New saves use format 4.
Migration supports this project's original v1 and tactical v2/v3 record layouts.
Format 3 adds the cover anchor to the NPC record. Format 4 extends the animation
tables with JO names. The version number alone does not establish
compatibility with every historical or modified Jedi Academy build.

Migration occurs in memory during loading. The source file is not rewritten.
Save to a new slot to keep the original file available for an older build.
Older builds are not expected to read new v4 saves.

The v1 reader consumes the original group and NPC layouts. It initializes only
the new report schedule and tactic fields: no active tactic, no tactical CP or
target ownership, zero deadlines, and zero goal/threat vectors. Existing progress,
NPC state, sight memory, groups, goals, and resources remain in the load path.
No AI cleanup function or blanket timer reset is used for migration.
The v1 and v2 readers set the new cover anchor to zero. Existing v2 cycles keep
their state and create an anchor before their first local peek.

The loader still checks checksums, chunk boundaries, and complete consumption.
Unsupported versions are rejected. Engine, game, and renderer modules must be
updated together because the shared save interface now exposes the loaded version.
The separate JK2 executable retains its format-1 policy. The JO campaign in the
shared runtime uses format 4. Original retail JO saves are not shared-runtime saves.

The readers for formats 1 through 3 consume the original animation-table length.
They initialize the new entries, then reload JO animation data where required.
Older staged JO actors receive the complete cinematic models and bone bindings.
Their state, surface overrides, attachments, and animation timers are retained.
See `jo-cinematics.md` for scene checks and replay limits.

## Verification

```bash
python3 scripts/test-save-migration.py \
  --old-package build/packages/20260913T151200115507047-44910296 \
  --package build/ready
```

The test uses vanilla by default for the new package. To test Rend2:

```bash
python3 scripts/test-save-migration.py \
  --old-package build/packages/20260913T151200115507047-44910296 \
  --package build/ready --renderer rdsp-rend2
```

The old package always uses vanilla to create a v1, v2, or v3 save. The test checks the
selected renderer identity for each process and rejects fallback.

The test creates a genuine old-format save with the older executable and game module.
It checks multiple NPCs and groups, personal/shared sight, targets, positions,
player health, armor, ammunition, weapon ownership, Force jump level, and objective
data. It saves the loaded game as v4, reloads it, and checks source-file hashes.
It also rejects files with valid checksums but unsupported versions 0 and 5.
A valid load must work after each rejection. The test uses OpenSSL's legacy
MD4 provider only to make these test files.

The v3-to-v4 check passes with Rend2. Earlier v2-to-v3 checks passed with vanilla,
and v1-to-v2 checks passed with
Rend2. Tests use a Linux GCC build with one build job, Xvfb, and LLVMpipe.
The squad `peek-save` case checks an active v3 cover pair across save/load.

This is not full campaign or historical-mod qualification. Do not force a file
through the parser if its prior record layouts differ from the supported layouts.
