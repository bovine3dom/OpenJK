# Datapad Automap

Open the datapad, then select **Map** between Mission and Weapons, or press **F4**.
The map shows the level's static BSP structure without exploration tracking.
It works in the JA runtime with either SP renderer and with JA or JO content.

## Controls

| Input | Action |
| --- | --- |
| Mouse wheel or + / - | Zoom in or out |
| Left mouse drag inside the map | Pan; the map follows the cursor |
| Right mouse drag inside the map | Raise or lower the selected height; drag up to raise it |
| Arrow keys | Pan the map |
| Page Up / Page Down | Move the height slice by 64 world units |
| [ / ] or Slice - / Slice + | Decrease or increase slice thickness by 64 world units |
| Q / E | Rotate the map |
| T or Tilt button | Switch between top-down and isometric views |
| Home or Player button | Centre on the player's position and height |
| Fit button | Fit the level's horizontal bounds |
| C or Control button | Centre on the next marked control, including its height |
| L or Lift button | Centre on the next lift |
| Escape or the datapad key | Close the datapad |

The map opens centred on the player. Zoom and view orientation are retained
until the level is reloaded. Map input does not move the player or the gameplay
camera. The normal datapad tabs remain available below the map.
Dragging starts only inside the map area. Releasing the button or closing the
page ends the drag. Toolbar and tab clicks remain available.

## Display

- The cyan arrow is the player. N indicates the map's positive Y direction.
- Filled areas show floors and slopes. Lines show structural boundaries.
- Ceilings are omitted. The default slice is 256 units thick, with 128 units
  above and below the selected height. `ui_automapSliceHeight` stores the full
  thickness in the profile. Its range is 64 to 4096 units.
- Gold squares mark active controls. Grey squares mark inactive controls.
- The status line reports the selected height and the total control count.
- Green lift markers and arrows show known travel destinations. Up/down cues
  remain useful in the top-down view. A question mark means that the route is
  unknown. Lift routes can remain visible when the platform is on another level.

This adapts the multiplayer automap's BSP-surface and height-aware approach.
Its old fixed-function OpenGL drawing code is not used. A shared UI geometry
path draws the map in both vanilla and Rend2. Coplanar triangle edges are merged
to reduce wireframe clutter. Curved BSP patches use a coarse tessellation.

## Switch and Button Coverage

Markers include explicit `func_button` entities and player-usable `func_usable`
or `misc_model_breakable` controls. Positions come from world-space entity
bounds, so brush controls do not incorrectly appear at origin zero.
Hidden entities, NPCs, ordinary props, doors, items, and trigger volumes are
not marked as controls. C cycles the marked controls without activating them.

Many retail wall switches are implemented with invisible use-triggers. Those
switches are not marked in this MVP. For example, the tested Kejim Base map has
four explicit controls, while Kril'dor has no explicit controls matching this
classification. This is a coverage limit, not exploration filtering.

The structural mesh is the static world model. Moving doors, lifts, removable
bridges, model props, and sub-BSP additions are not rebuilt into that mesh.
Control and lift markers use current entity state. The map does not infer whether a
scripted puzzle is complete or draw links from switches to their targets.

## Lift Routes

Native platforms use their mover endpoints. Named transport trains use their
path corners in travel order. Scripted lifts use motion destinations read from
ICARUS scripts, including tag positions and simple entity-parameter lookups.
The metadata reader does not execute scripts or their conditions. Consequently,
an indicated destination can still require a switch or story event.

Lift detection uses mover type, platform shape, and lift/platform names or sound
sets. Ordinary hatches, camera rigs, and moving machinery are not treated as
transport merely because they move. Arbitrary script expressions and routes
created after the metadata scan can remain unknown. Current mover endpoints
provide a fallback after movement has started. The map displays up to eight
known destinations per lift.

## Storage and Checks

Geometry is read from the installed BSP on first use and cached in memory.
The cache is cleared when the level state resets. No generated assets are
distributed, and the saved-game format is unchanged. Install the matching
engine and game module from the published package.

```sh
c++ -std=c++11 -I shared tests/automap.cpp -o build/automap-test
build/automap-test
python3 scripts/test-automap-sp.py
python3 scripts/test-automap-sp.py --renderer rdsp-vanilla
python3 scripts/test-automap-sp.py --campaign jo
```

The UI tests use headless windows and real keyboard/mouse events. They cover
the Map tab, geometry, height, tilt, pan, zoom, close keys, control markers,
drag release, slice thickness, lift routes, save/load, and renderer restart.
Use `automap_status` for diagnostic counts and
marker positions. Logs and images are under `build/smoke/automap.*`.
