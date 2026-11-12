# Gameplay rules: health, combat, inventory and interaction

[RulesWorkshop](../Projects/RulesWorkshop/README.md) is a compiler-free 2D/3D course that connects
health/shield, melee, death/respawn, collectibles, a medkit and a locked door to the existing
[motor/camera/checkpoint](Gameplay.md) system. WASD moves, Space jumps, Ctrl/left click attacks,
and E interacts. Touch the red hazard first, respawn, collect the golden key, defeat the guard,
use the medkit, unlock the blue door, collect the coin and reach the green finish.

## State ownership and authoring

`Gameplay::RuleWorld::For(scene)` stores one rules context in that scene's EnTT registry. Every
Blueprint host and native script sees the same health/inventory/interaction records. Records use
both UUID and generational entity handle: destroying and recreating an actor never inherits its
health, items or interaction uses. Different additive scenes have separate contexts; cross-scene
operations are rejected. Access is on the main thread.

Configure actors and objects with Blueprint Configure nodes in Event.Start. These node inputs are
ordinary serialized graph assets, editable through the existing node inspector, Save and Undo.
Mutable rule state is runtime data, not new ECS components and not part of scene serialization.
Play start/stop clears the context. Resetting rules does not undo transforms or SetActive changes:
reload the authored scene for a new game; the Editor uses a fresh runtime copy for each Play.
Reconfiguration requires Reset=true and is an explicit content-author operation.

| Node | Behavior |
|---|---|
| `Gameplay.HealthConfigure` | Maximum health, shield, hit grace, respawn delay and team. Existing health is preserved unless Reset=true. |
| `Gameplay.Health` | Configured/current/max/shield/dead/invulnerable/deaths/remaining respawn delay. |
| `Gameplay.Damage` | Shield absorbs first; overflow damages health. Optional shield/grace bypass. Source is optional environment damage; FriendlyFire controls matching nonzero teams. |
| `Gameplay.Heal` | Heal health or shield to its cap. Cannot revive a dead actor. |
| `Gameplay.Respawn` | Validate dead state and delay, restore a checkpoint, then refill health/shield and grant spawn grace. Failed restore cannot revive or consume inventory. |
| `Gameplay.InventoryConfigure` | Slots and per-item stack capacity; shrinking below existing contents fails without removing items. |
| `Gameplay.InventoryAdd/Remove` | Validated integer stack changes with no overflow, negative counts or zero-count slots. |
| `Gameplay.InventoryCount` | Per-item count and bag capacity/used slots. |
| `Gameplay.InteractionConfigure` | Prompt, enabled, max uses, cooldown, required/consumed key, reward, heal, deactivation and target point offset. |
| `Gameplay.InteractionFind` | Closest eligible visible object, prompt, distance, CanUse as Success, and rejection reason. Found can be true for a locked/full/cooling-down object. |
| `Gameplay.Interact` | Revalidate actor/object, range/facing/LOS, key/reward capacity and heal availability; commit the exchange once. |
| `Gameplay.Attack` | One closest visible living health target, facing/team filters, actual applied damage and victim UUID. A miss also consumes attack cooldown. |
| `Gameplay.RuleEvent` | Drain one actor event from a bounded FIFO. Has=false is an empty queue. |

Use separate player/enemy graphs: configure health once, then read or damage it from any host.
Direct damage ignores attack range by design (hazards, poison, scripted damage). Use Attack for a
range/visibility-checked melee action. A zero damage hit does not start grace. Overkill reports only
actual shield+health loss, and Killed is true only on the transition to zero health. Team zero is
neutral/unassigned; a nonzero team blocks friendly fire between different actors by default.
A living actor may damage itself. Native explicit damage can retain a dead source, for example a
projectile or poison application; Attack and Interact block dead/inactive users.

Lethal damage immediately clears character movement intent and Rigidbody2D horizontal velocity
(both axes for zero-gravity bodies). The motor automatically suppresses controlled movement for a
configured dead actor and invalidates cached jump/facing state across deaths, including skipped dead frames. The actor
stays active so its graph can wait for respawn and update its HUD; death does not automatically
destroy bodies, hide meshes or create a ragdoll. The sample deactivates a defeated enemy explicitly.
Respawn keeps the bag and already-used pickups/doors within the same run, avoiding duplicate rewards.
The checkpoint remains an actor position/rotation save. Use [opt-in gameplay snapshots](GameplaySave.md)
for disk-persistent health, inventory, interaction and AI; RulesWorkshop itself does not bind save slots.

## Atomic interaction

Interaction may require an item, consume it, award another item and heal. The complete prospective
bag and health change is checked before commit. A full bag, full health for a heal-only pickup,
missing key, inactive object, cooldown, exhausted uses or blocked/range-invalid query changes
nothing: no consumed key, awarded item, health change or used charge. Consuming the last key can
free its slot for a reward. Required and reward items may be the same key. Successful use commits
state and events before optional SetActive(false) lifecycle callbacks.

Use Range, Origin (world-space offset on the actor), Facing, MinDot, TwoD, LineOfSight and Mask
consistently in Find and Interact. Find does not authorize later use: Interact checks again after
movement or a door change. Distances are XY in 2D and XYZ in 3D. LOS uses actual Box2D/Jolt rays,
ignores sensors and actor/target bodies, and respects the collision layer mask. Object Offset is
world-space; configure it for the object's intended interaction point. Facing must be nonzero when
MinDot is greater than -1. Candidates are ordered by distance then full UUID for stable ties.
Attack uses the same Origin offset on attacker and target; this is a simple point-target melee
query, not animation hitboxes, swept weapon shapes, aim assist or authoritative network combat.

## Events, clocks and limits

Damage, Died, Healed, ShieldHealed, Respawned, Interacted, Collected and InventoryChanged events
carry Source/Subject entity IDs, item and amount. Inventory events contain signed committed deltas.
Use one consumer graph per actor; polling is destructive. The newest 32 events are retained and
Dropped counts discarded oldest events. Polling does not replace canonical Health/Inventory queries:
UI or gameplay that must survive a slow consumer should read current state. Transactions do not
invoke arbitrary script callbacks through this FIFO.

Hit grace, respawn delay, attack and interaction cooldown use the scene's scaled gameplay clock.
Pause/time scale zero stops those timers, and multiple graph hosts do not advance them repeatedly.
The Scene updates the clock once before gameplay updates. Query budgets and retired-handle cleanup refresh
every frame, including paused frames; refreshing them does not advance grace/cooldown timers. No wall-clock sleeps or graph-local timers are needed.

| Resource | Limit |
|---|---|
| Actor records / interaction records | 512 / 512 per scene |
| Inventory slots / item stack | 1–64 / 1–999999 |
| Item names | 1–64 ASCII letters/digits/underscore/dot/hyphen |
| Health, shield, damage, heal | finite, up to 1,000,000; max health is positive |
| Hit grace, respawn delay, cooldown, spawn grace | 0–60 seconds |
| Query range / origin or object offset | positive up to 100 / length up to 1000 |
| Expensive queries | 128 calls per scene update; at most 64 sorted candidates probed per query |
| Event queue | 32 per actor; oldest dropped on overflow |

Limits are runtime content safeguards. This is local gameplay state, not a server authority or save
anti-tamper system. Limits apply across graph hosts in the scene. Pure query nodes may evaluate for
multiple requested output pins, so avoid re-querying a large scene in unbounded graph loops.

## Native API

Include `Zamin/Gameplay/GameplayRules.h`. Obtain `RuleWorld::For(GetScene())` inside a running
native script, call Configure with HealthSettings, then Damage/Heal/Health, ConfigureBag/ChangeItem,
ConfigureInteraction/Find/Interact or Attack with RuleQuery. Returned health/bag values are snapshots;
mutating a snapshot does not change the actor. Native Revive resets health only; the Blueprint Respawn
node additionally restores the graph host's checkpoint. Prefer that integrated path for a player.

## Validation

`RulesModelTests.cpp` independently checks shield/health/death/grace and transactional bags.
`GameplayRulesTests.cpp` checks shared hosts, lifecycle, pause, UUID recreation, bounded queues/query
budgets, real Box2D/Jolt LOS, motor death gating, melee, failed/committed exchanges, respawn and graphs.
`Tests/rules_editor_test.py` exercises actual ImGui health/interaction/respawn node Save/Undo.
`python3 Tests/rules_workflow_test.py --build build --exports` runs real Player input, Editor replay,
compiler-free exports and screenshots for both scenes. Acceptance requires all eight events plus
canonical living player/dead guard, Key=0 and Coin=1. Export relocation checks the launcher version.
The private replay/observer overrides are disabled in the source project. Regenerate graph and scene
assets with `python3 Tools/samples/gen_rules_workshop.py`; all models are existing engine-owned fixtures.
