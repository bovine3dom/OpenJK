# Co-op Roadmap

## Purpose

Assess the work required to run the Jedi Academy single-player campaign with more
than one player.

This is an investigation document. It does not define a final design.

## Summary

Co-op is possible, but it is not a small merge of the SP and MP directories.
The work has two separate parts:

1. Adapt the SP game systems to a networked runtime.
2. Adapt each campaign level and script to support multiple players.

The second part may become the larger cost.

A useful first target is a two-player vertical slice on one simple campaign map.
Do not start with a full source-tree merge or full-campaign support.

## Current source layout

The main source trees are separate:

- `code/` contains the SP engine, client, server, UI, renderer, and game code.
- `codemp/` contains the MP engine, client, server, UI, renderers, and game code.
- `shared/` contains a smaller set of common systems.

The approximate source sizes are:

| Area | SP | MP |
| --- | ---: | ---: |
| Game code | 201k lines | 180k lines |
| Client code | 28k lines | 42k lines |
| Cgame code | 52k lines | 55k lines |
| Main source tree | 442k lines | 611k lines |

The line counts show the scale of the source. They do not measure the exact
amount of reusable code.

Some systems already have useful sharing:

- `shared/` contains common platform, math, string, and version code.
- `rd-common/` is shared by both renderers.
- Parts of Rend2 are shared by SP and MP.
- Both game trees contain NPC, scripting, vehicle, Force, and saber systems.

The gameplay code is still substantially different. It is not a common game
module with two small configuration layers.

## Main architecture differences

### SP runtime

The SP runtime has explicit single-client assumptions:

- `code/qcommon/q_shared.h` sets `MAX_CLIENTS` to `1`.
- Several server paths in `code/server/sv_main.cpp` loop over one client.
- SP advertises `sv_maxclients` as `1`.
- SP loads the game and cgame through a specialized native interface.
- SP uses `GAME_API_VERSION 13` and a large native import table.
- SP save games retain substantial game and Ghoul2 state.

### MP runtime

The MP runtime provides the network model needed for co-op:

- Multiple client slots.
- Network snapshots and user commands.
- Client prediction and interpolation.
- Separate game and cgame modules.
- A dedicated server.
- A different module API and VM/native-module path.

The MP game uses a different game API, data layout, and module boundary from the
SP game. Directly compiling the SP game into the MP runtime is unlikely to work
without a large adapter or a major API conversion.

## Recommended architecture

Do not try to merge every SP and MP file at the start.

Use the MP networking and server lifecycle as the foundation. Adapt the SP game
logic to that foundation through an SP-compatible game interface.

Preserve SP-specific systems behind that interface:

- ICARUS and campaign scripts.
- Cinematic control.
- NPC behaviour.
- Campaign progression.
- Save and load support.
- SP objectives, inventory, Force progression, and mission completion.

Move code into shared libraries only when the interfaces and ownership rules are
clear. A large conditional compilation layer would be hard to test and maintain.

## Campaign requirements

The campaign assumes one protagonist in many places. Each level may need review
for:

- Player spawn points.
- Script references to the player.
- Trigger and objective ownership.
- Doors, lifts, escorts, and switches.
- Cinematics and camera control.
- Player freezes and scripted movement.
- Mission completion and level transitions.
- Item, weapon, and Force progression.
- Player death and respawn.
- Late joining and disconnects.
- Save and load behaviour.

Some sequences can use a shared party state. Other sequences may need a named
host player, a list of active players, or a temporary scripted player lock.

Full campaign co-op should therefore be treated as a content port, not only an
engine feature.

## Jolt physics and co-op

Jolt is currently SP-only. Its main implementation is in
`code/game/g_jolt.cpp`.

It is more than a visual ragdoll system. It can change:

- NPC root position and velocity.
- Collision hulls.
- AI processing.
- Knockback.
- Grip and Lightning effects.
- Death and corpse state.
- Ghoul2 bone poses.

The implementation keeps physics records in a process-local actor map. The
current controller targets humanoid NPCs. It does not drive the first-person
player.

### Multiplayer authority

For gameplay-relevant physics, Jolt should run on the authoritative server.
The server should own:

- NPC root movement and velocity.
- Collision queries.
- AI state.
- Force effects.
- Death and recovery phases.
- Gameplay-relevant corpse state.

Clients should receive the result and render it. Independent full simulations on
all clients can diverge because of timing, floating-point differences, input,
and collision differences.

### Network representation

Normal snapshots can carry root state such as origin, angles, velocity, health,
and animation state. Full per-bone poses are more difficult.

The preferred first design is:

- Replicate authoritative root state and important physics events.
- Simulate or interpolate the visual limb pose on each client.
- Do not require exact per-bone determinism.

This permits visual differences between clients while keeping gameplay stable.
Exact server bone-pose replication is possible, but it would increase bandwidth,
complexity, and compatibility work.

### Dedicated-server changes

The current Jolt implementation uses SP-native Ghoul2 and game interfaces. It
must be split into server-safe and presentation parts before it can run in a
normal MP dedicated server:

- Server physics and state management.
- Server-side pose extraction where required for gameplay.
- Client-side Ghoul2 presentation.
- Network serialization of state and events.

The first co-op version should keep players on normal MP movement. Add
Jolt-driven player bodies only after NPC physics works.

### Jolt interaction rules

The co-op design must decide whether:

- Players can knock down or Grip each other.
- Players can block or push physical NPCs.
- Fallen NPCs block player movement.
- Players can stand on corpses.
- Jolt bodies collide with remote players.
- A physical corpse remains relevant outside a player's area of interest.

The current Jolt collision model excludes several object types. Extending it to
players or general dynamic objects would be a separate feature.

## Proposed phases

### Phase 0: Baseline and design

- Build the unmodified SP and MP targets.
- Record module APIs, structure sizes, and client lifecycle differences.
- Select the target number of players.
- Define host, join, save, disconnect, and progression rules.
- Select one simple campaign map.

Deliverable: a short technical design and a tested baseline.

### Phase 1: Networked SP gameplay slice

- Start an MP server with the SP game logic or an SP adapter.
- Connect two clients.
- Replicate player movement and player states.
- Replicate basic NPCs, weapons, damage, and pickups.
- Complete one map without complex cinematics.

Do not include full save games or all campaign systems in this phase.

### Phase 2: Campaign systems

- Add ICARUS and scripted entity support.
- Define player references in scripts.
- Add campaign objectives and progression rules.
- Handle level transitions.
- Add cinematic and camera policies.
- Test death, reconnect, and host departure.

### Phase 3: Jolt integration

- Move Jolt physics into the authoritative game server.
- Remove SP-only interface dependencies.
- Replicate root state and physics events.
- Add client-side pose presentation.
- Test multiple active physical NPCs and server CPU cost.
- Keep player movement non-Jolt at first.

### Phase 4: Save and load

- Define whether only the host saves or all players contribute state.
- Serialize multiple player states and campaign ownership.
- Serialize relevant Jolt corpse and recovery state.
- Test loading with the same and a different player roster.

### Phase 5: Campaign expansion

- Port one mission at a time.
- Keep a map and script compatibility list.
- Add automated tests for level start, objectives, transitions, and completion.
- Mark unsupported sequences instead of silently allowing broken state.

### Phase 6: Optional player physics

- Evaluate physical player bodies only after the campaign version is stable.
- Define prediction, reconciliation, collision, and recovery behaviour.
- Treat player ragdolls and physical Force effects as a separate project.

## Effort estimate

These are broad estimates for one or two experienced engine developers:

| Target | Estimate |
| --- | ---: |
| Architecture audit and design | 2–6 weeks |
| Two clients on an SP-derived test map | 3–6 months |
| Basic replicated NPC and combat systems | 3–9 additional months |
| One scripted mission | 6–12 months |
| Curated campaign subset | 1–2 years |
| Robust full campaign | 2–4+ years |

Volunteer development can take much longer.

## Recommended first milestone

Create a two-player test map or simple campaign map with:

- Two networked clients.
- Normal MP player movement.
- One or two SP NPC types.
- Basic weapons and saber combat.
- Damage, death, and respawn.
- One trigger and one objective.
- A basic level-complete transition.
- Optional server-authoritative Jolt NPC falls.

Success should mean that both players see consistent gameplay state and can
complete the map. Exact matching of every bone pose is not required.

## Overall assessment

A two-player campaign prototype is a realistic long-term engineering project.
A clean, complete unification of the SP and MP codebases is not required and
would add risk.

The most practical plan is to unify interfaces and shared systems gradually,
use the MP runtime for networking, preserve the SP game systems, and repair
campaign content through a tested map-by-map process.
