# Gameplay Typography

## Design Basis

These sources guided the first typography pass:

- [Xbox Accessibility Guideline 101: Text display](https://learn.microsoft.com/en-us/xbox/accessibility/xbox-accessibility-guidelines/101).
  It treats size, weight, spacing, case, and contrast as separate properties.
  Its Immortals Fenyx Rising example uses dark outlines to keep text visible
  against the game world. It recommends sentence case for longer text.
- [Game Accessibility Guidelines: Contrast](https://gameaccessibilityguidelines.com/provide-high-contrast-between-text-ui-and-background/).
  It recommends a plain background, or outlines and shadows when text must
  appear over an image.
- [Indieklem: Typography in game interfaces](https://indieklem.com/13-the-basics-of-typography-in-game-interface/).
  It uses Destiny 2 to explain hierarchy, and Frostpunk and Red Dead Redemption 2
  to explain contrast. It also stresses consistent sizes and controlled line lengths.

These sources support the design choices. They do not establish that this
implementation meets every accessibility guideline.

## Wheel Labels

The font family remains IBM Plex Mono. The labels use SemiBold rather than
Regular, a thin dark outline, slight letter spacing, and a wider text area.
Long weapon names can use two readable lines. The original name and casing
remain intact. Blanket uppercase conversion would make long labels harder to
read and would not solve the contrast problem.

The outline is restrained. It separates the letters from bright scenery
without adding a large panel or a heavy decorative border. Glyphs are rendered
at native pixel size. Both font weights and their license ship with the game.

## Gameplay Text

The gameplay pass uses Plex Mono Regular with a thin dark edge. Objective and
pickup notifications use SemiBold for emphasis. Its drawing
and measurement share the same metrics. This covers center messages, objective
and pickup notifications, subtitles, scrolling text, HUD labels, and numeric
HUD fields. Transparent console notifications also use Plex and wrap to the
screen width. Color codes and alpha fades remain available.

The gameplay font route is scoped to gameplay. The datapad now has a separate
Sans route, described below. Mission-complete screens use this route also.
Force and weapon selection use the separate RmlUi route described below.
Other menu text, credits, and the open console retain their existing fonts.
Loading screens also retain their existing path. Asian-language text keeps the legacy
font fallback because this bundled font does not supply those character sets.
Lettering painted into world textures is artwork, not a runtime text draw.

## Datapad

Datapad titles, tabs, lists, descriptions, objectives, and numeric fields use
IBM Plex Sans SemiBold. Original text colors are retained. The datapad has a
background, so this route does not add an outline or shadow.

Drawing, hit areas, and wrapping use the same proportional font metrics,
including kerning across color changes. The font is fitted inside the existing
row height. The row height itself does not change.
The visible capital height is aligned to the original font's visual center,
so titles, bullets, and selection highlights keep their original alignment.
Objective paragraphs use measured wrapping and the remaining panel height instead of estimated character
widths. Mission-complete headings, statistics, dialogue, and buttons use the
same font and alignment. Other menus use their existing font callbacks.

No separate UI or text scale setting is added.

Large text meshes are split into complete triangle batches within renderer
limits. Font textures and effects are released before renderer shutdown.

## Force and Weapon Selection

The shared JA and JO selection screens use IBM Plex Sans for standard text.
The JO point counter uses IBM Plex Mono. The help confirmation label keeps the
original `anewhope` display font and its joined Star Wars letters.
See [Force and Weapon Selection](rmlui-selection.md) for the layout and checks.

## Checks

```sh
c++ -std=c++11 -I shared tests/ui_text.cpp -o build/ui-text-test
build/ui-text-test
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-vanilla --typography
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-rend2 --typography
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-vanilla --datapad
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-rend2 --datapad
```

The headless checks exercise gameplay text, measurement, numeric fields, long
strings, menu isolation, and renderer restart. `testuitext` supplies a temporary
sample when `developer 1` is enabled. It does not change mission objectives.
