# HUD Reveal and Ally Compass

Hold **V** to show the status panels and all available contextual resource
indicators. A small compass band appears at the top of the screen. Release V
to return to the normal HUD.

The overlay works with both SP renderers and in both JA and JO campaigns.
It does not pause the game or change movement and weapon controls.

## Controls

```text
bind v +showhud
```

The first launch assigns V if it is free or still has the stock `+strafe`
binding. Other custom bindings are preserved. The engine supports two keys
bound to this action; release both keys to hide the overlay.

`+showhud` starts the reveal. `-showhud` ends it. `hud_status` prints the visible
state, camera heading, and ally marker data. `cg_hudReveal` is a read-only
engine state, not an archived setting.

While held, the overlay can reveal status information hidden by `cg_draw2D`,
`cg_drawHUD`, or `cg_drawStatus`. It does not change those saved settings.
Resource indicators return to their normal activity rules on release.
Opening a menu or console, losing focus, dying, loading a level, or restarting
the renderer clears the held state. Cinematics and remote-camera views suppress
the overlay.

## Compass

- N is the map's positive Y axis; E is positive X. This is a fixed local reference.
- The centre pointer follows the camera heading. The band covers 90 degrees
  to either side.
- Cyan diamonds show friendly characters. Edge arrows point toward allies
  outside the band, including those behind the camera.
- A small up or down mark indicates a height difference greater than 64 world
  units relative to the player.
- Up to eight allies are shown, ordered by distance. They can be tracked
  through walls and outside the current rendered view.
- Dead, hidden, hostile, and unspawned actors are excluded. Friendly NPCs must
  share the player's current team.

The compass does not require new map assets. Ally markers use the current game
entities, so movement, death, and save/load do not leave cached markers behind.

## Checks

```sh
c++ -std=c++11 -I shared tests/reticle_hud.cpp -o build/reticle-hud-test
build/reticle-hud-test
python3 scripts/test-hud-reveal.py
python3 scripts/test-hud-reveal.py --renderer rdsp-vanilla --width 960
python3 scripts/test-hud-reveal.py --campaign jo
```

The checks cover resource visibility, bearing wrap, behind-camera arrows,
height cues, multiple held keys, hidden HUD settings, console cleanup, ally
death, save/load, renderer restart, and movement while the overlay is held.
