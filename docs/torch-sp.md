# Weapon Torch

The JA runtime provides a player torch in Rend2 for both JA and JO campaigns.
Press **L** to toggle it. The default binding is added only if L is free and
the `torch` command has no existing binding. To bind L manually, enter:

```text
bind l torch
```

The torch starts off in a new profile. Its state and controls are saved in the
campaign profile. It does not consume inventory batteries.

## Controls

| Setting | Default | Function |
| --- | ---: | --- |
| `cg_torch` | `0` | `1` enables the torch; `0` disables it. The `torch` command toggles this value. |
| `cg_torchIntensity` | `4` | Light strength, from `0` to `16`. |
| `cg_torchRange` | `768` | Beam range in world units, from `64` to `2048`. |
| `cg_torchFov` | `50` | Full outer cone angle in degrees, from `20` to `100`. |
| `r_torchShadows` | `1` | Enable scene shadows from the torch. |
| `r_torchShadowMapSize` | `1024` | Shadow-map resolution, from `256` to `2048`. Use `vid_restart` after a change. |

All controls except shadow-map resolution change live. For lower brightness,
try `cg_torchIntensity 2`. For a wider beam, try `cg_torchFov 70`.
Use `torch_status` to print the active state, mount, position, and direction.

## Attachment and Shadows

In first person, the light starts just below the current weapon's muzzle tag.
It follows weapon movement and aims toward the camera's sight line. With a
hidden weapon, a scope, or a saber, it uses a position below and beside the
view instead. In third person, it stays with the player and uses the weapon
mount when current muzzle data is available.

A trace keeps the source on the player's side of a wall when the visible weapon
extends through it. The beam has a warm-white centre, a soft cone edge, and
distance falloff. Opaque lit surfaces receive diffuse and specular lighting.
Skin diffusion also receives the torch's diffuse light.

The renderer draws one perspective depth map for the torch. World geometry,
doors, props, and character models can cast shadows. The first-person weapon
is excluded from shadow casting. Nine depth samples filter the shadow edge.
This does not require shadows for all dynamic lights or ray tracing.

The shadow texture is allocated on first use and retained until renderer
shutdown. An enabled torch adds a shadow pass. Use a smaller shadow map to
reduce its cost. The effect requires Rend2; vanilla has no torch-lighting pass.

The torch is suppressed during cinematics, death, intermissions, remote-camera
use, vehicle use, and mounted-weapon use. Its enabled setting is retained.
UI portraits and sky portals do not receive its light.

## Checks

```sh
python3 scripts/test-torch-sp.py
python3 scripts/test-torch-sp.py --msaa 4
python3 scripts/test-torch-sp.py --campaign jo
```

The tests use headless hardware rendering and isolated profiles. They compare
lighting and a character's wall shadow with off/restored controls. They also
check weapon changes, hidden-weapon fallback, wall clipping, third-person use,
cinematic suppression, save/load, and renderer restart with the other raster
features enabled. Logs and screenshots are under `build/smoke/torch.*`.

The scene descriptor change requires the matching engine, game module, and
renderer from the published package. The saved-game format is unchanged.
