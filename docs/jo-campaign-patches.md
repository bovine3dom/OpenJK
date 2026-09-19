# JO Campaign Patches

## Asset Separation

Campaign changes are specified as edit recipes in `scripts/jo-patches.json`.
A recipe contains entity names, expected values, and changes. The native
importer contains matching checked edits. The package does not contain JO maps,
scripts, Python, or the source recipe file.

The launcher runs the importer only for `--campaign jo`. The importer reads the
player's local JO installation and writes `OpenJK/zz_jo_campaign.pk3` in the
separate `campaigns/jo` profile. Patched entity files go into that archive.
They are not installed in the shared JA game directory.

JA-only players need only JA assets. They can launch JA without a JO asset path.
Building the normal package does not read JO assets. The optional JO integration
tests require those assets.

## Recipe Format

Each top-level key is a map name, such as `kejim_post`. Its recipe has these fields:

- `description`: the purpose of the change.
- `edit`: an ordered list of edits to existing entities.
- `add`: complete entity definitions to add at the end of the entity list.

Each edit has a `match` object and an `expect` count. All fields in `match` must
equal the source values. An optional `exclude` object removes matching entities
from that selection. The `set` object changes or adds fields. The optional
`remove` list deletes fields.

The importer checks the match count before it applies each edit. A mismatch
stops the import and leaves the previous archive in place. This prevents a
recipe for one map version from silently changing a different set of entities.

The importer uses an existing JO `.ent` file when available. Otherwise, it reads
the BSP entity list. It writes `maps/MAP_NAME.ent` and keeps the original BSP
geometry. No map compiler or engine change is required for these entity edits.

The import cache includes a native format version and source archive records.
Increase that version when a native patch changes. The next JO launch then
rebuilds the local archive. Original game files remain unchanged.

## Kejim Post: Jan at the Door

The original `st_death` events serve two counters. Two deaths send Jan into
combat. Seven deaths start `check_bigdoor`.

The patch gives the six ground guards a separate `jo_ground_death` event.
The door counter waits for six of these events. A relay also sends each ground
death to `st_death`, so the earlier combat trigger still counts all seven guards.
The bridge guard keeps its original death event and is not part of the door
counter.

Jan can now run to the door, shoot it, and continue the dialogue while the bridge
guard is alive. The original movement, shooting, and dialogue scripts are used.

Start Kejim Post again to apply this entity change. An existing mid-map save
retains its saved entities and counter values; rebuilding the import does not
rewrite those save records.

## Nar Shaddaa Starpad: Fuel Symbols

The fuel-symbol textures are part of the map geometry. The scripts do not send
an event when the player sees a symbol. The patch adds one `target_passcode`
marker to each source symbol. It does not change the fuel controls or the puzzle
scripts.

The importer checks the two `func_usable` fuel controls before it adds the
markers. The check confirms the original script names and frame ranges. If the
source map does not have the expected controls, the import stops.

The game records a marker when the player is close to the symbol, looks toward
it, and has an unobstructed view. The marker count is part of the saved entity
state. See [Passcode Overlay](passcode-overlay.md) for the supported codes and
test procedure.

Import format 3 and later formats include this patch. Start Nar Shaddaa Starpad
from a new game or from a save that the game made after format 3 was imported. An old mid-map
save can contain the old entity list without these markers.

## Checks and New Patches

For a new patch, update the source recipe and the matching checked native edit.
Increase the native import format version. Add a synthetic importer test and a
focused runtime test. Keep unrelated entities and story triggers intact.

```bash
python3 scripts/test-import-jo.py
python3 scripts/test-play-sp.py
python3 scripts/test-jo-cinematics.py --case jan-door --package build/ready
python3 scripts/test-jo-cinematics.py --case jan-door --package build/ready --renderer rdsp-rend2
```

The importer and launcher tests use synthetic assets. The runtime test uses
the generated JO entity file. It places Jan in the courtyard and kills the
ground guards through the normal NPC death handler. It checks that five ground
deaths are insufficient. After the sixth, it checks her shooting, dialogue,
and the tower door opening while the bridge guard stays alive.
