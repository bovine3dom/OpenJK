# Physical Melee Reactions: Deferred Plan

Status: deferred. Regional control, Grip, and Lightning are implemented and have
automated checks. Complete their normal-play review before saber reactions start.

## Scope

Keep native attack timing, damage, parries, saber locks, and special moves.
Use accepted hits to control the victim's physical response.
Keep the weapon arm strongly controlled during attacks and parries.
Allow smaller reactions in the torso and legs.

| Event | Proposed response |
| --- | --- |
| Blocked saber strike | Small recoil that preserves the guard |
| Nonlethal body hit | Local rotation and balance correction |
| Knockdown | Physical fall |
| Lethal hit | Guided collapse with the strike direction |
| Floor finisher | Native attack and damage, then passive settling |
| Kick or blunt strike | Bounded directional impulse |

Combine repeated trace samples from the same contact into one bounded reaction.
Keep separate strikes and native damage events separate.
Saber cuts should cause local loss of control rather than large launch impulses.

## Dismemberment

A severed limb must stop receiving motor targets and providing support.
Disable or remove the affected bodies and joints before using a severed rig.
Keep native detached-limb handling. Use native reactions for unsupported cases.

## Regional Control Requirements

Allow separate settings for torso, head, arms, legs, and feet.
Keep posture state separate from temporary effects such as Grip and Lightning.
Use animation poses as motor targets. Keep collision and weapon traces consistent
with the authoritative physical pose. Full physical duelling is a separate task.

## References

- [Epic: physics-driven animation](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-driven-animation-in-unreal-engine)
- [ProcHitReact: per-bone control and repeated reactions](https://github.com/Vaei/ProcHitReact)
