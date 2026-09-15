# Kejim NPC Class and Pressure Checks

## Confirmed Defect

The original JO definitions use class values such as `stormtrooper`, `jan`, and
`galak_mech`. JA requires `CLASS_STORMTROOPER`, `CLASS_JAN`, and `CLASS_GALAKMECH`.
The initial importer copied the JO values without conversion. The JA parser
stored class `-1` for these NPCs.

The published game already contained the JA AI improvements. However, its
tactical eligibility check rejected class `-1`. NPCs could acquire an enemy and
join a group while remaining ineligible for pressure retreats, cover cycles, and
flanks. Other class-specific controllers could also be bypassed.

The importer now converts the class names. The alias `galak_mech` needs an
additional underscore correction. Names that already start with `CLASS_` remain
valid. The import cache rebuilds automatically after an updater launch.

## Save Repair

Earlier MVP saves can contain class `-1`. During JO save loading, the game reads
the correct class from the imported definition for that NPC type. It changes
only the invalid class. It does not parse the NPC's other properties again.

A save made with the previous published build was tested. Jan and five native
guards recovered their classes. The selected guard retained its saved health,
position, and script flags. The repair does not change the save format or rewrite
the source save file. Other campaign encounters still need testing.

## Native Guard Test

Run from the repository root after packaging:

```bash
python3 scripts/test-jo-sp.py --package build/ready --ai
```

The build script also runs this check when JO assets are available. Results are
under `build/jo-tests/`, and the package records `jo-ai-result.txt`.

The test uses the original `kejim_post` spawners, NPC definitions, and scripts.
It waits for `st_guard2` to finish scripted movement and acquire Kyle through
normal perception. It does not assign an enemy through a diagnostic command.
The player moves to a fixed test position. Both the player and the selected
guard are protected from incidental damage.

Tactics are disabled during setup. The test then enables them and fires the
player's Bryar pistol beside the guard. It checks:

- Valid classes for native NPCs.
- Enemy acquisition and sight before any test damage.
- A real incoming-fire event and unchanged guard health after the missed shot.
- Running retreat movement of at least 32 units.
- Unchanged script flags during the retreat.
- Class and health preservation across save/load.
- A running response to five damage through the NPC damage handler.

Before the fix, the selected guard retained class `-1`, had no incoming-fire
pressure response, and stayed at its position. With the fix, a shot about 58
units from the guard caused a retreat while its health stayed at 30. A separate
observation also showed an autonomous flank before any damage.

## Remaining Limits

Near-miss pressure requires an acquired enemy and an eligible combat group. It
does not itself assign an enemy or create sight memory. JO's opening scripts
temporarily restrict alerts and set navigation tasks. Cinematics, pending
scripted movement, and explicit freezes retain control.

Supported ranged classes use the JA pressure policy. Droids and saber users
retain their own class-specific controllers; valid classes do not assign the
trooper cover controller to every NPC. The separate no-group formation
controller and full attention/FOV behavior remain outside these checks.

Complete the Kejim playtest in `human_todo.md` to assess encounter difficulty,
Jan's routes, reactions before enemy acquisition, and retreat quality in other
parts of the map.
