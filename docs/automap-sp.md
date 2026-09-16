# Datapad Automap

Open the datapad, then select **Map** between Mission and Weapons, or press **F4**.
The map shows the level's static BSP structure without exploration tracking.
It works in the JA runtime with either SP renderer and with JA or JO content.

## Controls

| Input | Action |
| --- | --- |
| Mouse wheel or + / - | Zoom in or out |
| Left mouse drag inside the map | Pan; the map follows the cursor |
| Arrow keys | Pan the map |
| Page Up / Page Down or Floor + / Floor - | Centre on the next floor in the exploded view |
| Q / E | Rotate the map |
| T or Tilt button | Switch between top-down and isometric views |
| Home or Player button | Centre on the player's position and height |
| Fit button | Fit the full map bounds |
| C or Control button | Centre on the next marked control, including its height |
| L or Lift button | Centre on the next lift |
| X or Explode button | Switch between whole-map and exploded views |
| Escape or the datapad key | Close the datapad |

The map opens centred on the player. Zoom and view orientation are retained
until the level is reloaded. Map input does not move the player or the gameplay
camera. The normal datapad tabs remain available below the map.
Dragging starts only inside the map area. Releasing the button or closing the
page ends the drag. Toolbar and tab clicks remain available.

## Display

- The cyan arrow is the player. N indicates the map's positive Y direction.
- Filled areas show floors and slopes. Lines show structural boundaries.
- Ceilings are omitted. The normal view includes all map heights.
- Vertical structural edges have one-sixth opacity in both views.
  Floor outlines and route markers keep their usual opacity.
- Gold squares mark active controls. Grey squares mark inactive controls.
- The status line reports the view mode and the total control count.
- Green lift markers and arrows show known travel destinations. Up/down cues
  remain useful in the top-down view. A question mark means that the route is
  unknown. Lift routes can remain visible when the platform is on another level.

This adapts the multiplayer automap's BSP-surface and height-aware approach.
Its old fixed-function OpenGL drawing code is not used. A shared UI geometry
path draws the map in both vanilla and Rend2. Coplanar triangle edges are merged
to reduce wireframe clutter. Curved BSP patches use a coarse tessellation.

## Exploded View

Press **X** or select **Explode** to build a Recast navigation mesh for the map.
The first build occurs when you select this view. The mesh stays in memory
until the level state resets. A build failure keeps the whole-map view available.

The navmesh supplies height bands for the original BSP map. Large, approximately
level areas supply the reference elevations. A 48-unit tolerance combines
nearby elevation samples. The reference does not move as samples are added.
Slope boundaries and 16-unit elevation bands remain separate during mesh
generation. This keeps stairs and ramps from removing the landing boundaries.
An elevation peak must have at least 4096 square units of support and 10 percent
of the largest peak's support to create another floor. This reduces small
platform and stair-tread bands.

The display keeps the original map's filled surfaces, height shading, and
structural outlines. It does not draw the navigation polygons. Band boundaries
are halfway between adjacent reference elevations. BSP surfaces and edges that
cross a boundary are split there. Each piece keeps its original world geometry.

The layout puts the bands in rows, with the highest band first. Their projected
bounds include the BSP surfaces and edges, so the bands do not overlap.
Each band keeps its shape, scale, and compass orientation.
Disconnected areas can share a band. These bands are estimates, not room names
or architectural storeys. Small landings can join the nearest main band.

- **F1**, **F2**, and subsequent labels identify bands from lowest to highest.
  **Z** gives the reference elevation in world units.
- The cyan arrow shows the player. The status line identifies the player's band.
- Tan lines show navigation-polygon connections across bands.
  Nearby links between the same two bands share one line when both endpoints
  are within 128 world units of a representative connection.
- Green lines show possible lift routes. These routes can require a story event.
- Gold diamonds show active controls. Grey diamonds show inactive controls.
- **Page Up / Page Down**, or **Floor + / Floor -**, centres the view on the
  next band. **Player** returns to the player at a local zoom. **Fit** shows all bands.
- Drag with the left mouse button to pan. Zoom, rotation, and tilt remain
  available. Rotation and tilt fit the layout again.

This MVP uses static BSP surfaces marked solid. It includes ceilings for
clearance tests. It does not reconstruct collision brushes, moving geometry,
or AI-only restrictions. The mesh is for display; it is not an AI route source.
The build uses 16-unit horizontal cells, 4-unit vertical cells, 56-unit clearance,
16-unit radius erosion, a 16-unit step limit, and a 45-degree slope limit.
Input and grid-size limits bound the build. Large levels can use the whole-map view
if they exceed these limits.

The first configuration downloads Recast 1.6.0 at commit
`6dc1667f580357e8a2154c28b7867bea7e8ad3a7`. Only the Recast library is linked.
Its source is cached in the CMake build directory. Its license is installed
under `licenses/recast`.

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
c++ -std=c++11 -I shared -I build/sp/cache/automap_recast-src/Recast/Include \
  tests/automap_nav.cpp shared/qcommon/automap_nav.cpp \
  build/sp/code/libautomap_recast.a -o build/automap-nav-test
build/automap-nav-test
python3 scripts/test-automap-sp.py
python3 scripts/test-automap-sp.py --renderer rdsp-vanilla
python3 scripts/test-automap-sp.py --campaign jo
python3 scripts/test-automap-layout.py --map yavin_trial
python3 scripts/test-automap-layout.py --map artus_mine
```

The UI tests use headless windows and real keyboard/mouse events. They cover
the Map tab, whole-map geometry, tilt, pan, zoom, close keys, control markers,
drag release, lift routes, exploded bounds, save/load, and renderer restart.
Use `automap_status` for diagnostic counts and
marker positions. Logs and images are under `build/smoke/automap.*`.

The layout check compares the title, toolbar, tabs, and cursor area before and
after map changes. Artus Mine exercises Rend2's geometry-buffer reuse with a
large exploded map. Add `--buffer-storage` to check persistent mapped buffers.
Layout captures are under `build/smoke/automap-layout.*`.
