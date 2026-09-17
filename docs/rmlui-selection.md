# Force and Weapon Selection

JA and JO use RmlUi for the Force and weapon selection screens. The screens use
the original JA backgrounds, icons, rank markers, and animated scanlines.
The layout has a 640 by 480 reference area. It scales equally in both directions
and stays at the center of the display. Wide displays have side bars. Tall
displays have top and bottom bars.

## Controls

- Click a power to add a rank. In JA, click the chosen power again to undo it.
- In JO, use **Undo upgrade** to remove a rank added during this selection.
- Select **Weapons** to continue to weapon selection.
- Select two main weapons and one explosive type. Click a chosen weapon to
  remove it before you select a replacement.
- In JO, select **Force Powers** to return to the Force screen.
- Select **Begin mission** to apply the choices and load the next map.
- Use Tab, Shift+Tab, or the arrow keys to move focus. Use Enter or Space to
  activate the selected control. Escape closes the help panel.

JA permits one optional Force upgrade. Its core powers stay under story control.
JA weapon availability and mission transitions use the original menu rules.
JO uses the budgets and story rules in [JO Mission Preparation](jo-mission-preparation.md).
Choices stay in memory until confirmation. A renderer restart retains them.
Loading a save clears them. Save the game after mission preparation is complete.

## Fonts and Assets

Standard labels, descriptions, and help text use IBM Plex Sans. The JO point
counter uses IBM Plex Mono. The original `anewhope` display font is retained
for the help confirmation label. This preserves its joined Star Wars letters.

RmlUi loads `ui/rmlui/selection.rml` and `selection.rcss` through the game file
system. The custom `ja-shader` element draws native game shaders. This keeps
their blend modes and animation. The `star-wars-text` element draws the original
display font at pixel coordinates. The package contains the layout and Plex
fonts. The original artwork comes from the local game assets.

The UI adapter in `code/ui/ui_selection.cpp` keeps JA choices separate from
live equipment. Confirmation runs the original weapon and mission scripts.
JO uses the existing `jo_prepare` commands. The renderer and input code are in
`code/client/cl_selection_menu.cpp`.

If RmlUi is disabled at build time, or the layout file is missing, the original
menu system is used. JO then uses `ui/jo_preparation.menu`.

## Checks

Use `rml_selection_status` in the console to inspect the page, focus, layout,
Force ranks, and weapon choices.

```bash
python3 scripts/test-rmlui-selection.py --package build/ready
python3 scripts/test-rmlui-selection.py --package build/ready --renderer rdsp-rend2
python3 scripts/test-jo-preparation.py --package build/ready
python3 scripts/test-jo-preparation.py --package build/ready --renderer rdsp-rend2
```

These headless tests use mouse and keyboard input. They check selection limits,
help, renderer restart, mission start, and save loading. The JA test also checks
image placement and input at standard, wide, and tall display sizes.
