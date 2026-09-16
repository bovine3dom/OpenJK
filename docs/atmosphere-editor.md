# In-Game Atmosphere Editor

## Open the Panel

Use the current published Rend2 build. During gameplay on a map with a valid
atmosphere profile, open the console and enter:

```text
atmosphere_editor
```

The mouse now controls a RmlUi side panel. The local game pauses while the panel
is open. The scene stays visible beside it. Scroll the panel to reach all fields.
Press Escape or click **Close / Esc** to return to the game.

The editor requires RmlUi, Rend2, and a supported cube-sky profile. A stock map
without a profile reports that requirement in the console. Map changes and
renderer restarts close the editor and release its input capture.

## Controls and Visualizations

- Drag a slider for a broad adjustment. Small coefficients use logarithmic
  sliders. Use the adjacent number box for an exact value.
- Click **?** beside a field for its meaning, units, and valid range.
- RGB swatches show relative channel balance. They are not predictions of the
  final sky color. The absorption swatch shows the channels being removed.
- The sun compass shows the direction toward the light. North is +Y and east is
  +X. Its center is overhead. The readout gives azimuth and elevation.
- The density bars compare Rayleigh and aerosol density at 0, 1, 5, and 10 km.
- **Apply preview** validates the draft, rebuilds the lookup table, and enables
  the atmosphere. It can take about a second. Slider movement alone does not
  rebuild the table.
- **Show stock / Show atmosphere** switches the last applied sky. Draft edits
  still need Apply.
- **Reset from file** reads the active file again and resets the draft.

Color and strength controls are near the top. Geometry controls are lower in the
panel. The sky material is editable, but Apply requires a cube sky referenced by
the current map. The file-format version is fixed at `atmosphere 1`.

## Copy the Profile

Click **Copy file + filename**. The clipboard contains a complete, valid profile
with its repository filename in a comment, for example:

```text
// File: scripts/maps/shared/ja-korriban.atmosphere
// Edited on map: kor2
atmosphere 1
sky textures/skies/korriban
...
```

The actual clipboard contains every parameter, not the ellipsis shown above.
Paste the whole result into the named file, or send it to the coding assistant.
The filename comment is accepted by the profile parser. Copy exports the draft,
including valid changes that have not yet been applied.

The editor follows the active filesystem lookup and resolves map symlinks.
A shared profile exports its `scripts/maps/shared/...` filename. A private local
override exports `scripts/maps/<map>.atmosphere`. Archive sources and links
outside the map directory use the map filename as the export target.

Apply and Copy do not write files. The preview lasts until a map load, renderer
restart, or profile reload. Reopening the panel reads the last applied settings;
unapplied edits are discarded when the panel is reopened.

If you have local campaign or review overrides, they still take priority over an
updated package. After committing an export, update the matching active override
too, or remove it to use the packaged version. The **?** beside Sky material shows
the active source path. Saving a shared file affects its aliases when they load.

## Diagnostics and Checks

`atmosphere_editor_status` prints the map, export filename, pause state, and
current draft/applied brightness. An optional element ID prints its rectangle
for input debugging. The panel uses the same parameter bounds as the renderer.
Invalid or non-finite input is rejected without replacing the applied preview.

```sh
python3 scripts/test-atmosphere-editor.py
```

The test uses a headless X11 window, real mouse and keyboard events, and `xclip`
to read the desktop clipboard. It checks sliders, numeric edits, help, live
apply/toggle behavior, invalid input, shared/private filenames, unchanged source
files, player input capture, renderer restart, and map changes.
