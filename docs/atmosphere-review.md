# JA and JO Atmosphere Review

## Audit Result

The audit covers 102 retail BSPs: 60 single-player maps and 42 multiplayer maps.
There are 33 SP profiles: 23 for JA and 10 for JO. The other 27 SP maps retain
stock rendering. Multiplayer maps are listed in the audit but are outside this
SP rollout. See [the map-by-map decisions](atmosphere-map-audit.md).

The profiles are initial candidates for manual review. `t1_sour` retains its
approved settings. Other palettes match the source art, with restrained blends
for painted planets, forests, and cloud decks. The audit examined assets and
shader references; it did not run a complete playthrough of each map.

Volumetric clouds remain deferred. See [the cloud investigation](volumetric-clouds.md).

## Start a Review Session

Close the game, then start the published build:

```bash
openjk-play --worktree squad_ai --campaign ja --desktop --atmosphere-review
```

This visits the JA maps that have profiles. To include maps intentionally left
stock, append `all`:

```bash
openjk-play --worktree squad_ai --campaign ja --desktop --atmosphere-review all
```

Use a separate launch for JO:

```bash
openjk-play --worktree squad_ai --campaign jo --desktop --atmosphere-review
```

Add `all` for all JO SP maps. JO requires the local Outcast assets configured
with `openjk-play --configure-jo`. The first JO review launch creates its own
import archive. Later launches reuse it.

The launcher uses an `atmosphere-review` directory below the worktree profile.
JO uses `campaigns/jo` within that directory. Review settings, key bindings,
screenshots, and notes are separate from the normal campaign profile.

For a local package, use:

```bash
bash build/ready/launch-sp.sh /path/to/GameData --campaign ja --desktop --atmosphere-review
```

## Controls

| Key | Action |
| --- | --- |
| Page Down | Next map; wraps at the end. |
| Page Up | Previous map; wraps at the start. |
| Home | Reload the current review map. |
| F4 | Leave the current scripted camera. |
| F5 | Toggle the atmosphere, with other enhancements held constant. |
| F6 | Reload the current map's edited atmosphere profile. |
| F7 | Disable atmosphere and save the stock capture. |
| F8 | Enable atmosphere and save the profile capture. |
| F9 | Log an `ATMO_TWEAK` marker and position; save a numbered screenshot. |
| F10 | Print the map, palette, decision, atmosphere state, and position. |
| Space / Ctrl | Fly up / down. |

Use normal movement and mouse-look controls. Open the console with Shift+Esc
to read status and markers. Each map starts with cheats, flight, and NPC freeze
enabled after the opening sequence. Exposure is fixed for comparisons. Analytic
haze, local fog, and reconstructed sky assets are disabled during this review. The
review holds 30 FPS during setup waits, then returns to 60 FPS.

Most starting positions come from BSP player-start entities. They are not
verified outdoor camera points. If the map starts indoors, fly to an opening
or through the roof. `t1_sour` uses its verified rooftop view. Cinematic-only
academy maps can take control again or advance to another map; use F4 or skip
them with Page Down. If a scripted menu captures the keys, open the console and
enter `vstr ar_next`. F10 also prints the actual engine `mapname`.

To jump directly to a map, enter, for example:

```text
exec atmosphere-review/ja/all/t1_danger.cfg
```

For JO, use `atmosphere-review/jo/all/bespin_streets.cfg` in a JO review session.
JA and JO use separate processes because their asset and campaign state differ.

## Edit and Record Results

The launcher prints the full profile and notes paths. Each selected map has an
editable copy in the review profile's `OpenJK/maps/<map>.atmosphere` directory.
Edit that file and press F6. Restarts and build updates retain these local edits.
Delete an override file before relaunching to adopt the latest packaged version.

Start with these fields:

- `skyBlend`: overall replacement strength. `0` keeps the source sky; `1` uses
  the full atmosphere result within its sky mask. Older profiles default to `1`.
- `illuminance`: atmosphere brightness.
- `mie`, `absorption`, and `groundAlbedo`: dust and horizon color.
- `cloudColor` and `cloudStrength`: retained cloud-layer appearance.
- `sunDirection`: direction of the scattering source.

Most new profiles keep `sunDisk 0` to avoid adding a second disc to painted art.
The daylight solver requires an above-horizon source. Night and space scenes
therefore retain their stock skies. A low blend retains most artwork, but it can
still reduce the contrast of a painted planet or tree. Check those features.

F7 and F8 write `atmo_<campaign>_<map>_stock.png` and
`atmo_<campaign>_<map>_profile.png` under `OpenJK/screenshots`. They replace the
previous pair for that map. Use `screenshot_png` without a name for extra captures.

Record decisions in `atmosphere-review-notes.csv`. Suggested status values are
`approved`, `tweak`, and `stock`. The game does not edit this CSV automatically;
F9 records a quick marker in the console log. Include a position and the issue,
such as a faded planet, an incorrect horizon color, or excessive brightness.
The current log is `OpenJK/qconsole.log`. Before each review launch, the previous
log is copied to `OpenJK/review-logs/` with a timestamp.

To retain changes in the project, copy the edited files back to `scripts/maps/`
and update the review decisions. The build validates and packages those files.
Palette seeding creates only missing files; it never replaces per-map edits.

## Scope of This Pass

Profile values and shader targets were checked against the asset inventory.
The review launcher is checked with a fake game, including campaign separation
and retention of edits. The normal single-job build runs both renderer smoke
checks. One headless JA tour check also verified entry, next-map navigation,
profile loading, and a capture. Full-map rendering tests and new benchmarks
are not part of this pass.
