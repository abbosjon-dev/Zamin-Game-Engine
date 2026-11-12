# Quests and progression

[QuestWorkshop](../Projects/QuestWorkshop/README.md) connects real 2D/3D collectibles, combat,
interaction, position triggers, rewards, XP and [gameplay saves](GameplaySave.md). Collect three
runes, defeat the sentry, activate the beacon, reach the exit and claim the survey reward. Return
home for the dependent quest. A fresh process resumes the saved book and rejects duplicate claims.

## Definitions and state

`Gameplay::QuestWorld::For(scene)` owns books by actor UUID plus generational handle. Different
scenes do not share books; destroying/recreating an actor never inherits its quests. Configure
living, active actors with health first, then define quests in Event.Start. Definitions belong to
the actor rather than a transient quest-giver; HUD owners may disappear independently. Play
stop/start clears books and bindings. Access is main-thread only.

A scene supports 32 quest actors, each with at most 16 definitions. Each definition has a stable
key, title, 1–8 uniquely keyed objectives, up to eight prerequisites and up to eight item rewards.
Keys/receipts use 1–64 ASCII letters, digits, underscore, hyphen or dot. Titles are limited to 128
bytes, objective labels to 96 and matching tags/items to 64. Goals are 1..1,000,000; reward item
counts 1..999,999 and reward XP 0..1,000,000. Configure prerequisites first: forward references,
self references and duplicate prerequisites are rejected, so configuration cannot create cycles.
Definitions are immutable during that run; duplicate Configure does not reset progress or rewards.

Modes: Locked -> Active -> Ready -> Claimed. Start requires every prerequisite Claimed, not merely
Ready. Failure is terminal from Active/Ready and preserves objective evidence. There is no automatic
restart, repeatable-quest reset or hidden reward re-grant. A new game or explicit snapshot restore
provides a different timeline. Dead/inactive actors cannot start, advance, fail or claim.

| Kind | Progress source |
|---|---|
| 0 Manual | QuestAdvance with objective key, positive count and unique receipt. |
| 1 Collect | Committed positive ChangeItem/interaction reward grants matching the item key. Removals do not subtract progress. |
| 2 Kill | A lethal health transition caused by this actor; optional subject tag filter. Repeated damage to a corpse does not count. |
| 3 Interact | A successful shared-rule interaction; optional object tag filter. Failed/exhausted interactions do not count. |

Automatic notices run after gameplay commits and before interaction deactivation callbacks. They do
not drain or depend on the bounded RuleEvent FIFO. An interaction reward is counted once, despite
InventoryChanged and Collected both being emitted for other consumers. Matching is exact; an empty
kill/interact match accepts any subject. Multiple matching objectives can advance from the same
committed action. Each notice scans at most 16*8 objectives for that actor; no asynchronous notice
queue silently drops progress. Collection is cumulative grants after Start, not current ownership.

Manual advances clamp to the goal, require an Active quest and only target Manual objectives.
Receipts deduplicate triggers across the whole quest, persist in saves and are bounded to 64.
A duplicate/invalid/full receipt set changes neither counts nor mode. Use batched counts or automatic
objectives for high-volume progression. Native Notice is a trusted integration API; Blueprint authors
use the committed gameplay operations or the explicit receipt API. This is local gameplay state,
not server authority or an anti-cheat ledger.

## Atomic rewards and XP

Claim prepares all prospective inventory exchanges first. A full bag, stack overflow or invalid
state leaves bag, XP and Ready mode unchanged, allowing retry after freeing capacity. Success commits
all item rewards, XP and Claimed mode before emitting events. A repeated claim cannot grant again.
Reward inventory events remain available to UI/other consumers, but quest reward grants deliberately
do not recursively advance collection objectives.

The initial level curve costs `100 * currentLevel` XP to advance, up to level 100. Total XP is bounded
to 1,000,000,000; Current/Needed/Fraction describe the current level, with Needed=0/Fraction=1 at the
cap. LevelUp reports the resulting level once even if a reward crosses several levels. XP currently
comes from claimed quest rewards; spending XP, custom curves, skills and equipment are separate
systems. Save validation requires XP to equal the configured XP sum of Claimed definitions.

## Blueprint authoring

| Node | Behavior |
|---|---|
| `Gameplay.QuestConfigure` | Quest/title, parallel Objectives/Labels/Kinds/Matches/Goals arrays (equal length 1–8), Requires array, Rewards/RewardCounts arrays and XP. |
| `QuestStart` | Start a configured Locked quest after claimed prerequisites. |
| `QuestAdvance` | Manual objective, Count and receipt; repeated receipt is rejected. |
| `QuestFail` | Fail Active/Ready without granting anything. |
| `QuestClaim` | Atomic reward transaction. |
| `QuestState` | Configured, title/mode, equal-weight fraction, completed objective count, Ready/Claimed. |
| `QuestObjective` | Label, current/goal and completion for one objective. |
| `QuestProgression` | XP, level and current-level fill. |
| `QuestEvent` | Pop Started/Objective/Ready/Claimed/Failed/LevelUp; bounded 32 per actor, Dropped saturates. |
| `QuestBindHUD`, `QuestUnbindHUD` | Bind/release text/progress widgets to a whole quest or named objective. |

Abbreviated names have the Gameplay prefix. Commands return Success/Reason; state reads are
canonical. Events are destructive telemetry and may overflow, so do not use their delivery as the
authority for rewards or completion. Objective event Amount is the submitted notice magnitude;
read State/Objective for clamped canonical counts.

HUD binding supports at most 128 distinct UITransform text/progress widgets. Dynamic text must not
have a LocalizationKey; fonts, overflow, colors and layout remain authored UI content. Bindings cannot
compete with gameplay HUD/inventory bindings for the same widget. Overall progress is the average of
objective fractions; single-objective binding shows its label and current/goal. They refresh before
UI input and after gameplay, including pause. Suspended owners freeze their display, destroyed
owners/widgets release it, and missing/recreated source actors display an unavailable fallback.
Unbinding restores original text/progress values. Menus themselves remain the [UI kit's](GameplayUI.md)
responsibility.

## Save v2 and migration

Base saves without quest books still encode as v1. Capturing a bound actor with quests selects v2;
each entity includes a Quests object with XP and keyed mode/count/receipt records. The checksum,
2 MiB file bound, strict duplicate/unknown-field rejection and backup policy remain unchanged.
The content signature includes complete quest definitions, including titles, goals and prerequisites.
Keep those stable or explicitly change the project's schema for incompatible content revisions.

V2 restore validates definition/key sets, counts against goals, mode consistency, prerequisite
coherence, receipts and XP before committing any actor. Rule inventory and quest records swap in the
same main-thread commit, so loading a claimed book does not invoke Claim or grant its rewards again.
Pending quest events are cleared. Unbound actor books are outside that snapshot, like other opted-in
gameplay state. Saving/rewinding a whole profile intentionally rewinds both rewards and claims.

V1 migration preserves old health/bags/poses and introduces currently authored quest definitions as
Locked with zero counts, receipts and XP. Existing non-quest signatures still have to match, including
scene/schema, body layout, graph path and rule configuration. It does not migrate arbitrary asset
renames or reinterpret old quest history. An explicit native GameSave containing quest books must
use Version=2. Unknown versions remain rejected. Snapshot integrity checks are corruption protection,
not authentication of user-edited local files.

## Verification

`Quest*` native tests cover model bounds, receipts, terminal modes, levels, actual rules notices,
multiple reward failure, prerequisites, lifecycle/generations, HUD conflicts, v1/v2 codecs,
transactional rejection, backup recovery and replay prevention.

`python3 Tests/quest_workflow_test.py --build build --exports` runs real keyboard actions in both
dimensions, then restarts to complete a dependent quest, then restarts again and attempts both claims.
Editor auto-play repeats all three phases. Compiler-free exports write, resume in another process
and reject duplicate rewards after relocation. Auto phase preferences only select QA behavior;
canonical quest modes, inventory and XP establish acceptance. `Tests/quest_editor_test.py` exercises
real ImGui QuestConfigure/QuestClaim/QuestBindHUD add/save/Undo. Reports: `build/GameplayQuest/`.
