# Coordinated Combat Research

## Sources

- [Imperial War Museums: A Short History of the Winter War](https://www.iwm.org.uk/history/second-world-war/eastern-front/short-history-of-the-winter-war). Directly read. It describes terrain knowledge, leadership, and resilience as advantages for a smaller defending force.
- [WFYI: Finnish Tactics](https://media.wfyi.org/fireandice/history/tactics_finnish.htm). Directly read. It describes mobility, local terrain knowledge, and cohesion under a large equipment disadvantage.
- [Building the AI of F.E.A.R. with Goal Oriented Action Planning](https://www.gamedeveloper.com/design/building-the-ai-of-f-e-a-r-with-goal-oriented-action-planning). Directly read. Short plans need validation during execution. Movement, actions, and communication make coordination visible.
- [Squad Coordination in Days Gone](https://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter12_Squad_Coordination_in_Days_Gone.pdf). Reviewed through a Kagi-generated summary because normal fetching returned binary PDF data. The summary describes confidence, spatial lanes, stable assignments, and distinct regroup/attack states. This is secondary evidence, not a direct quotation from the chapter.

## Game Adaptation

The objective is a coordinated threat to a Jedi, with readable counterplay.
Use these high-level principles as game design inputs:

| Principle | Game behaviour | Player counterplay |
| --- | --- | --- |
| Cohesion | Nearby groups with the same enemy join through local member contact. Retreats prefer positions with support. | Separate members or interrupt their local contact. |
| Useful terrain | A retreat can end at a position where another member can cover an approach. | Stop pursuing, change route, or approach from another direction. |
| Shared effort | One member moves while a capable member fires. A supported flank gets priority over an optional exposure move. | Pressure the supporter or block the route. |
| Short plans | Rally, hold briefly, then resume attack when a current observation supports it. | Deny fresh observations; the plan must expire. |
| Controlled attack pressure | Coordinate existing grenade users and limit simultaneous throws. | Use the warning time and the existing Force-return mechanics. |

All positions must pass game collision, route, ownership, and friendly-fire checks.
Reports use recorded observations. A retreat is not proof that the player will
pursue, and gunfire is not proof that the player is distracted.

## Acceptance

Check native campaign actors as well as controlled fixtures. Measure squad joins,
completed flanks, supported retreats, route failures, and actual arrivals. Check
that a blocked route causes a new decision. Keep normal health and damage.
Audit voice clips before assigning a new spoken meaning to an event.
