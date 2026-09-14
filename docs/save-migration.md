# Save Migration

Jedi Academy accepts project save formats 1 and 2. New saves use format 2.
The supported migration target is a save from this project's pre-tactics builds
with the original v1 record layouts. The version number alone does not establish
compatibility with every historical or modified Jedi Academy build.

Migration occurs in memory during loading. The source file is not rewritten.
Save to a new slot to keep the original v1 file available for an older build.
Older builds are not expected to read new v2 saves.

The v1 reader consumes the original group and NPC layouts. It initializes only
the new report schedule and tactic fields: no active tactic, no tactical CP or
target ownership, zero deadlines, and zero goal/threat vectors. Existing progress,
NPC state, sight memory, groups, goals, and resources remain in the load path.
No AI cleanup function or blanket timer reset is used for migration.

The loader still checks checksums, chunk boundaries, and complete consumption.
Unsupported versions are rejected. Engine, game, and renderer modules must be
updated together because the shared save interface now exposes the loaded version.
Jedi Outcast remains on its existing format-1 policy.

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

The old package always uses vanilla to create the v1 save. The test checks the
selected renderer identity for each process and rejects fallback.

The test creates a genuine v1 save with the older executable and game module.
It checks multiple NPCs and groups, personal/shared sight, targets, positions,
player health, armor, ammunition, weapon ownership, Force jump level, and objective
data. It saves the loaded game as v2, reloads it, and checks source-file hashes.
It also rejects files with valid checksums but unsupported versions 0 and 3.
A valid load must work after each rejection. The test uses OpenSSL's legacy
MD4 provider only to make these test files.

These checks passed with Rend2. Tests used a Linux GCC build with one build job,
Xvfb, and LLVMpipe. These are software-rendering results.

This is not full campaign or historical-mod qualification. Do not force a file
through the parser if its prior record layouts differ from the supported v1 layout.
