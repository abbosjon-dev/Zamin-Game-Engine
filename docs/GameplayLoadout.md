# Equipment, combat stats and abilities

[LoadoutWorkshop](../Projects/LoadoutWorkshop/README.md) connects native equipment, combat,
energy, abilities, consumables, quest rewards and version 3 [gameplay saves](GameplaySave.md).
The compiler-free 2D/3D sample uses the same operations for player input and automated acceptance.

## Ownership and stat calculation

`Gameplay::LoadoutWorld::For(scene)` owns runtime records by actor UUID and generational handle.
Configure living health first, then the loadout. An actor supports 32 immutable item definitions,
16 immutable ability definitions, four equipment slots and 32 queued events. A scene supports 64
loadout actors. Duplicate definitions/configuration are rejected rather than resetting progression.
Destroy/recreate, Play stop/start and scene changes do not inherit another actor's state.
Access is main-thread only; definitions are authored in Blueprint or native setup code.

Stats are a five-number array: **Attack, Armor, Power, Maximum energy, Energy regeneration**.
Base values must be finite 0..10,000, with maximum energy at least one. Item modifiers may be
negative, within -10,000..10,000. Equipped modifiers add to base stats, then clamp to
0..100,000 (maximum energy: 1..100,000). Removing capacity clamps current energy; increasing
capacity does not grant free energy. Regeneration follows scaled game time and stops for dead,
inactive and paused actors. Equipment does not change the separately configured maximum health.

Slots: 0 Weapon, 1 Offhand, 2 Body, 3 Trinket. An item definition assigns one legal slot.
Equip removes one item from the shared bag and returns the previous item in one prepared exchange.
Unequip returns the owned item to the bag. Stack/capacity/missing-item failures change neither the
bag nor equipment. Repeated equip is rejected. Equipped items have explicit ownership outside the
bag; generic bag consumption cannot consume them. Equip/unequip emit inventory notifications but
do not forge collection quest grants. There is no durable item instance, rarity or affix database yet.

For positive shared-rule damage, a different source actor's Attack adds to the requested damage.
The victim's Armor multiplies it by `100 / (100 + Armor)` before shield, hit grace and death rules.
Zero damage stays zero; self/environment damage gains no outgoing Attack. Native/Blueprint attacks,
AI attacks and damage abilities use this path. PierceShield bypasses shields, not armor.
Damage is bounded to 1,000,000. The hot damage path computes small stat arrays without copying
an actor's ability book. It remains local gameplay authority, not a multiplayer anti-cheat system.

## Abilities and consumables

Kinds: 0 Damage, 1 Heal, 2 Shield. Define a key, Amount (.001..100,000), energy Cost (0..100,000),
Cooldown (0..300 seconds), Range (.001..100), optional Ingredient and Count (1..999,999), and the
initial Unlocked flag. Effective effect magnitude is `Amount + Power`; damage also follows the
shared Attack/Armor calculation. Unlock/lock changes availability without clearing uses/cooldowns.

Damage requires an explicit live enemy. Shared rule perception checks range, layer mask, 2D/3D
mode and optional physics line of sight. Team checks, shields, invulnerability and kill/quest notices
remain shared combat rules. Support abilities target self; an explicit different target is rejected.
Heal and Shield clamp to their configured caps. A full-health heal or full-shield cast has no effect.

Cast validates unlocked state, energy, cooldown and ingredient availability, then prepares the bag
and progress changes. A successful effect commits energy, consumed items, cooldown and a saturating
use count. Failed/out-of-range/friendly/invulnerable/no-effect attempts preserve those resources.
Per-ability cooldowns are independent of the normal attack cooldown. Scaled gameplay ticks advance
regeneration and all cooldowns; dead/inactive actors freeze both. Save restores remaining durations,
so restarting cannot clear a cooldown. Refill explicitly grants bounded energy, clamped to capacity.

This package supplies immediate single-target effects. Cast bars, projectiles, area effects,
stacking buffs, an effect graph editor and network prediction remain separate work.

## Blueprint and HUD

| Node | Purpose |
|---|---|
| `Gameplay.LoadoutConfigure` | Define the actor's five base stats. |
| `Gameplay.ItemDefine` | Immutable Item, Slot and five Modifiers. |
| `Gameplay.EquipmentEquip`, `EquipmentUnequip` | Transactional ownership transfer with the shared bag. |
| `Gameplay.EquipmentState` | Configured, equipped flag and item key for a slot. |
| `Gameplay.LoadoutStats` | Effective Attack/Armor/Power, current Energy/Maximum/Regen/Fraction. |
| `Gameplay.AbilityDefine` | Define the effect, costs, ingredient and initial availability. |
| `Gameplay.AbilityUnlock` | Enable/disable a configured ability. |
| `Gameplay.AbilityCast` | Effect transaction; returns Success/Applied/Victim/Reason. |
| `Gameplay.AbilityState` | Availability, cooldown remaining/fraction, uses and cost. |
| `Gameplay.LoadoutRefill` | Restore energy; returns applied amount. |
| `Gameplay.LoadoutEvent` | Pop Equipped/Unequipped/Unlocked/Locked/Cast/Energy telemetry. |

Abbreviated names use the Gameplay prefix. Configure/equipment/definition commands return
Success/Reason. Pure state nodes do not mutate gameplay. Ready describes actor/resource/timer
readiness; the chosen target's range/LoS and whether the effect can apply are checked at Cast.
Cooldown Fraction is 0 at a new cooldown and 1 when ready (zero-duration cooldown: 1).
Events are a destructive FIFO, 32 per actor; Dropped saturates. Read canonical state for authority.

The sample composes live HUD text from state nodes and SetUIText each LateUpdate. It displays
slots, stats, energy, cooldown, cast count, health, potions, enemy health, XP and coins. Normal
[gameplay UI](GameplayUI.md) owns modal focus/pause; [quest HUD](GameplayQuest.md) remains available.

## Save v3 compatibility

Capturing a bound loadout actor selects v3; scenes without loadouts retain v1/v2. V3 contains
nullable Loadout state on every entity: Energy, four ordered equipped-item keys and keyed ability
Remaining/Uses/Unlocked records. Quests retain their v2 representation. Strict keys, finite ranges,
unique equipment keys, 16-ability bounds, checksums, duplicate JSON-key rejection and the 2 MiB bound
apply. The compatibility signature includes base stats and all item/ability definitions.

Restore stages bag, health, quests, equipment and abilities before committing. It validates slot
ownership against definitions, effective energy capacity, ability sets and cooldown caps. Loading
neither re-equips items nor grants rewards: owned equipment and the bag are restored together.
Changing definitions requires an explicit content/schema migration; it does not silently reinterpret
old bonuses. Unbound actors remain outside a snapshot.

V1/v2 migration strips only the new Loadout signature contribution, retaining existing configuration
checks. It restores legacy bags/health/quests and initializes current authored loadouts with no
gear, full base energy, zero uses/cooldowns and configured initial unlock flags. Equipped-item history
cannot be inferred from old saves. Unknown future versions and attempts to encode loadouts in
v1/v2 are rejected. Local integrity checks detect corruption, not deliberate user save editing.

## Reproducible checks

`Loadout*` unit tests cover ownership/capacity rollback, modifiers, combat, casts, resources,
team/range/invulnerability rejection, lifecycle, legacy migration, malformed saves and Blueprint
catalog/dispatch. `Tests/loadout_workflow_test.py --build build --exports` runs three fresh Player
processes and three Editor phases per dimension, then compiler-free export/write/resume and a real
relocated no-replay run. Canonical equipment, ability uses, energy, quest XP and bag establish the
oracle. Automation preferences choose a test phase only; they are not gameplay state authority.
`Tests/loadout_editor_test.py` checks actual ImGui add/save/Undo. Reports: `build/GameplayLoadout/`.
