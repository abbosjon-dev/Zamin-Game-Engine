# Gameplay AI: patrol, perception and combat

[AIWorkshop](../Projects/AIWorkshop/README.md) contains compiler-free 2D and 3D NPC courses.
AI uses the existing scene navigation, physics and [shared combat rules](GameplayRules.md).
This package supplies a finite-state controller; behavior trees, GOAP, multiplayer authority and
humanoid animation assets remain separate work.

## Authoring

Configure Health and NavAgent first, then call AIConfigure in Event.Start. Supply a matching baked
2D/3D grid (or streamed-world navigation), team, target tag, optional explicit victim, patrol points,
view/lose distances, FOV, memory, leash, attack thresholds, speed and recovery limits.
Relative points are offsets from the home captured at configuration; absolute points are world coordinates.
All route points must be finite and inside the home leash. There are at most 64 points per actor and
32 configured brains per scene. Invalid configuration is rejected without replacing a working controller.
2D distances use XY; 3D sensing uses XYZ and navigation moves on XZ. For a 3D body use a feet-level
entity root with the collider offset upwards, so the root can be projected onto the navigation grid.
Static bodies and mismatched 2D/3D agents are rejected.

| Blueprint node | Behavior |
|---|---|
| `Gameplay.AIConfigure` | Configure once; Reset explicitly replaces existing configuration. |
| `Gameplay.AIState` | Read canonical mode, target, goal, last seen, memory, counters and status. |
| `Gameplay.AIEnable` | Suspend immediately or resume the existing controller. |
| `Gameplay.AIReset` | Clear target/recovery state and return home; Rehome captures a new home after validation. |
| `Gameplay.AIRemove` | Cancel movement and release navigation ownership. |
| `Gameplay.AIEvent` | Pop one mode transition and its target at transition time. |
| `Gameplay.Sense` | Bounded hostile perception with range, facing, tag, team, near range and physics LOS. |
| `Gameplay.Strike` | Attack precisely the supplied victim; never substitute a closer actor. |

Actor defaults to the host. Optional entity references accept Self as the graph host; an empty victim
permits automatic acquisition. Sense and Strike can also be used independently to build custom graphs.
Sense candidates are configured, living, active hostile health actors. Team zero is unassigned.
Near range bypasses the FOV test, not LOS. Strike revalidates range/LOS/team and shares the combat
cooldown clock; a miss consumes cooldown. Use State for authoritative reads: the event FIFO is
bounded to 32, destructive, and reports dropped events rather than growing without limit.

## State and movement

The controller patrols with waits, ping-pong or loop progression. A single patrol point is counted once
until interrupted. Visible targets trigger Chase or Attack, with separate enter/exit attack distances to
avoid threshold oscillation. Losing sight investigates the last seen point for the memory duration.
Destroyed targets are forgotten immediately. Memory expiry, target leaving the home leash or actor
leaving the leash triggers Return; return finishes before reacquiring enemies. Dead and Disabled
stop motion. Blocked is terminal until an explicit reset.

`Gameplay::AIWorld::For(scene)` owns native records in that scene's registry, keyed by UUID plus
generational handles for actor, owner and victim. Different scenes do not share controllers. Destroying
and recreating an entity cannot inherit a prior brain. Prefabs configure independent controllers in
their own OnStart. Configuration graph inputs are serialized; mutable AI state is runtime-only.
Play stop/start clears records. Access is main-thread only.

The scene ticks controllers after scripts and nav obstacles, before navigation movement. Scripts do
not have to call an update node. Paused scene time does not advance memory or wait clocks; pruning
still removes destroyed owners. Disabling the owning script, pausing/faulting its Blueprint VM,
inactivating the actor or losing its required components stops motion. Death stops the agent and
clears target memory; revival returns home. Enable does not clear a terminal Blocked state.

AI owns NavAgent speed, stopping distance, repath timing and recovery while installed; the original
values are restored on removal or scene reset and the previous destination remains paused. Existing
active PatrolPath or SplineFollower owners are rejected. Do not simultaneously drive the same actor
with a motor, formation controller or custom movement script. Movement uses SetNavDestination and
existing Rigidbody2D, CharacterController3D or Rigidbody3D adapters, rather than teleporting the actor.
Stopping cancels pending world queries and traversal tickets and clears horizontal movement intent
(including both axes for 2D movement); vertical 3D velocity remains available to gravity.

Both local and streamed navigation use bounded stall detection, cooldown and retry recovery.
Waiting on asynchronous queries or explicit portal traversal does not spend stall retries. AI never
completes a portal itself: gameplay must perform and acknowledge the traversal. Disable, remove or
reset cancels stale queries/tickets. Exhausted retries enter Blocked; resetting permits another attempt.

## Budgets and evidence

Perception reuses the rules scene budget of 128 queries per frame and scans at most 512 health
records, with at most 64 LOS candidates per query. Each brain reserves capacity for one sense and
one strike. A depleted budget defers work and stops movement without throwing or advancing the FSM.
This bounds work, not frame time on arbitrary content or hardware.

Run `python3 Tests/ai_workflow_test.py --build build --exports` for actual Player input replay,
Editor auto-play and compiler-free exported runs in both dimensions. The oracle requires patrol,
chase, attack, investigate, return, death and independently navigating prefab reinforcement, plus
canonical health/state checks. `Tests/ai_editor_test.py` exercises real ImGui add/save/Undo.
Native tests cover lifecycle, budgets, ownership, physical motion, LOS and world portal cancellation;
BrainModel tests cover pure state transitions. Reports and screenshots are under `build/AIGameplay/`.
