# Force Power Wheel

## Controls

The SP client adds **Force power wheel** to **Controls > Force Powers** in the
main menu and the pause menu. Select that row, then press a key or mouse button.
The normal binding system saves the assignment with the other controls.

The initial default is **G**, if G is free and the wheel has no other binding.
Middle mouse keeps its existing saber-style binding. To use middle mouse for
the wheel, assign it in the menu. The menu then removes its previous assignment.
The existing L binding can still change saber style.

Console equivalents:

```text
bind G +forcewheel
bind MOUSE3 +forcewheel
```

Hold the assigned button to open the wheel. Move the mouse toward a power,
then release to select it. Use the normal **Use Force Power** binding to activate
the selected power. Direct Force bindings still work when the wheel is closed.

Releasing in the center dead zone leaves the selection unchanged. Escape
cancels the wheel. Focus loss, menus, death, cinematics, renderer restart, and
changes to the available powers also cancel it. A cancelled held key cannot
reopen the wheel through keyboard repeat.

In the dead zone, the wheel highlights the currently selected power and shows
its name. Pointing at another sector highlights that power without selecting
it until release.

Q/E use the existing previous/next Force bindings. They select immediately and
show the radial wheel briefly instead of the bottom selection bar. This display
does not slow time, capture the mouse, or block gameplay actions. Repeated steps
restart its display timer. Cycling while the held wheel is open ends mouse
selection and its slow-time effect; releasing the held button does not undo the
cycled selection.

Weapon scrolling uses a separate radial display with the same input-free,
normal-time behavior. The latest Force or weapon selection replaces the other
display. See [Weapon Wheel](weapon-wheel.md).

## Display And Input

The wheel contains the known, selectable Force powers. Passive abilities do
not have sectors. Low energy does not remove a power or move its sector.
The layout stays fixed while the wheel is open. Small sector margins prevent
selection changes caused by small mouse movements at a boundary.

RmlUi draws the circular sectors, cursor, and power name. The wheel keeps the
game's Force icons and localized names. The circle and icons use aspect-correct
dimensions. The label uses bundled IBM Plex Mono Regular, rendered by FreeType
at the framebuffer's pixel size. It is not a scaled bitmap font. Font atlases
do not use game texture detail, gamma adjustment, compression, or mipmaps.
See `ui/fonts/plex/README.md` for the pinned source and license.

The mouse controls the wheel instead of the camera. Movement remains active:
you can walk, strafe, jump, crouch, and change between walking and running.
Movement held before opening continues, and movement keys can change while the
wheel is open. Closing the wheel does not stop movement. Attacks, interaction,
and explicit Force activation are suppressed. Their held inputs are cleared
on open and close. Selection does not activate a power.

The wheel applies a 0.2 multiplier to game time. It does not write to `timescale`.
An existing time scale, or a change made by Force Speed or another game action,
therefore remains in effect after the wheel closes. Mouse selection uses input
events, not scaled game time.

This version supports mouse selection with a keyboard or mouse-button binding.
Gamepad sector selection is not implemented.

## Tests

Run from the repository root. Build with one job:

```sh
bash scripts/build-sp.sh
c++ -std=c++11 -I shared tests/force_wheel.cpp -o build/force-wheel-test
build/force-wheel-test
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-vanilla
python3 scripts/test-force-wheel.py build/ready --renderer rdsp-rend2
```

The headless test needs Xvfb, xdotool, Xlib, and FFmpeg. It uses a fresh profile
and saves logs and screenshots under `build/force-wheel-tests`. It checks real
key and mouse input, release and cancel behavior, slow time, camera capture,
continued movement, blocked combat inputs, focus loss, restart, death, and menu
binding persistence. It also captures the font after a renderer restart with
reduced texture detail and a different game texture filter.

`forcewheel_status` prints the current wheel state, selected slot, available
slots, time scale, and input state. It can help diagnose an input problem.
`forcewheel_defaults` assigns G only if it is free and the wheel is unbound.

Human checks are still required for visual quality, icon readability, mouse
feel, and interaction during combat on hardware graphics drivers.
