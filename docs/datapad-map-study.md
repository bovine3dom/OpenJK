# Datapad Map Investigation

Status: research only. No map implementation is included.

## Recommendation

Use 3D exploration data with a simple cutaway display. Start with a north-up
top-down view, a height selector, and automatic tracking of the player's level.
Add an optional isometric view of the same data if it helps with stairs and
shafts. A freely rotating 3D map is not required for the first prototype.

A top-down view is easier to read when retracing corridors. An isometric view
can explain height changes, but upper floors and ceilings can hide useful
detail. Both views need height filtering. Keep the current level clear and
dim or hide the other levels. Use the compass convention: north is positive Y.

## Existing Code

- `codemp/rd-vanilla/tr_world.cpp` contains an old multiplayer wireframe automap.
  It extracts world surfaces and filters their display by height. Its generation
  marks the non-solid world nodes visible; it does not supply exploration history.
- `codemp/cgame/cg_view.c::CG_DrawAutoMap` supplies zoom and view rotation.
  It is tied to multiplayer scenes and team-game conditions.
- Rend2's `stub_InitializeWireframeAutomap` in `codemp/rd-rend2/tr_init.cpp`
  returns success without implementing that renderer path. This is not a
  feature that can be enabled in SP with a setting.
- `code/qcommon/cm_public.h` exposes point-to-leaf lookup, PVS, area connectivity,
  and collision traces. These can help build and reveal local map geometry.
- `code/cgame/cg_main.cpp` has datapad drawing entry points for objectives,
  weapons, inventory, and Force powers. The datapad still uses legacy menu
  structure with the newer text rendering. A Map tab needs its own drawing and
  input path; it does not require replacement of the other tabs.

Use the old automap as a reference. Keep new map data separate from renderer
state so that vanilla and Rend2 can display it. A simplified mesh or sampled
floor cells could be projected into the existing UI geometry path. Compare
these approaches in the prototype before choosing one.

## Exploration Rules

- Record the player-controlled body's route, not cinematic or remote-camera motion.
- Reveal a small visited region and its nearby visible boundaries. Do not reveal
  an entire BSP cluster just because it is potentially visible.
- Keep height in the visited data. Walking below a room must not reveal that room.
- Use traces and area connectivity to prevent reveal through walls and closed doors.
- Break the recorded trail at teleports and map changes. Do not join those points
  with a line through unexplored space.
- Show only discovered controls within explored areas. A later change to switch
  highlighting policy can be separate from geometry discovery.

BSP leaves are rendering and collision partitions, not authored rooms or floors.
A flat XY mask or a PVS-only mask would reveal too much. Sparse 3D cells or
walkable-surface records are better candidates. Budget discovery work and update
only when the player moves far enough; do not scan the full level every frame.

## Switches

Inspection of the local stock entity lumps produced these counts:

| Map | `func_button` | `func_usable` | Use-button trigger candidates |
| --- | ---: | ---: | ---: |
| JA `t1_sour` | 0 | 4 | 2 |
| JA `t2_wedge` | 0 | 22 | 17 |
| JO `kejim_post` | 0 | 139 | 46 |
| JO `kejim_base` | 0 | 85 | 38 |

The last column counts `trigger_multiple` and `trigger_once` with spawnflag 4.
It includes other controls, such as camera terminals, and is not a confirmed
switch count. None of these trigger candidates has an explicit `origin` field.
Use transformed brush bounds or a discovered interaction point for their markers.

`code/game/g_utils.cpp` contains `ValidUseTarget`, `G_IsTriggerUsable`, and
`CanUseInfrontOf`. These account for usable flags, inactive state, facing,
trigger volumes, and script targets. Reuse or factor this logic instead of
labelling every `func_usable` as a switch. The current prompt query returns a
boolean; map discovery would also need the control's identity and position.

Use stable map-local identities rather than reusable runtime entity slots.
Distinguish discovered, previously activated, and inactive controls. Activation
does not prove that a puzzle is complete: scripts can reset controls or require
several actions. Avoid promising door destinations from target links alone.

## Persistence and Datapad Input

Keep exploration with the save, not in one global profile bitmap. Loading an
older save must not reveal places reached later in another save branch.
Key map data by campaign and effective map content. Include imported entity
overrides when identifying switch metadata.

`code/game/g_savegame.cpp::WriteLevel` and `ReadLevel` use ordered chunks and a
final `DONE` marker. Add a versioned exploration chunk with an older-save
fallback. Old saves have no route history; begin recording from the loaded
position. Derived static geometry can use a separate local cache.

Add a dedicated Map tab with pan, zoom, height controls, and a return-to-player
action. Keep map input separate from player movement and camera angles. Update
the datapad tab and close-key handling in `code/ui/ui_main.cpp` and
`code/ui/ui_shared.cpp` when adding the page.

## Suggested Prototype Checks

Compare top-down and isometric views on `kejim_post`, `kejim_base`, and
`t2_wedge`. Check stacked corridors, stairs, lifts, narrow doors, and large open
areas. Verify that closed rooms and upper floors remain hidden. Test control
discovery before and after use, scripted state changes, old saves, save branches,
map transitions, and renderer restarts. Measure map generation and discovery cost.

As a design reference, [The Force Engine's automap source](https://github.com/TheForceEngine/TheForceEngine/blob/master/TheForceEngine/TFE_DarkForces/automap.cpp)
separates explored walls, layers, map controls, and saved state. Dark Forces has
authored sector layers; JA and JO BSP maps need their own height model.
The [Force Engine development notes](https://theforceengine.github.io/2022/11/27/torwards-1.0.html)
also identify map scrolling that affected the player as a bug to avoid.
