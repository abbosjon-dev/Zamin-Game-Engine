# Gameplay save/load

[SaveWorkshop](../Projects/SaveWorkshop/README.md) demonstrates compiler-free 2D/3D progress saves:
health, inventory, a consumed interaction, AI patrol memory and a persistent prefab slot. WASD moves,
E applies example progress, Ctrl saves and Space loads. Close the Player and reopen it, then load to
continue. The acceptance harness also corrupts the primary file and checks backup recovery.

## Stable slots and content compatibility

Bind each participating scene root once with a unique key such as Player, Door or Guard. Keys and
slot/schema names contain 1–64 ASCII letters, digits, underscore or hyphen. One scene owns at most
256 bindings. A UUID plus generational handle protects each live binding; destroying and recreating
an entity never silently reuses the binding. Explicitly bind a replacement to its existing key.
Different scenes have independent contexts and cross-scene binds are rejected. Play stop/start clears
bindings. Use `Gameplay::SaveWorld::For(scene)` from native code; access is main-thread only.

`SaveSpawn` creates an authored prefab inside Assets and binds its root to a key. Repeating that
request returns the same live instance. Recreate these slots on each startup before loading; the
saved state uses keys, so newly generated UUIDs are remapped correctly. Prefab OnStart must finish
configuring health and AI before Load. Spawn is limited to eight attempts per frame. Files never
supply executable scripts or asset paths to instantiate during Restore. Child geometry follows its
root; independently animated child transforms are not included in this root snapshot.

Loading requires the exact same binding keys, scene name and author-supplied Schema. A configuration
signature checks health settings, bag capacity, interaction settings, AI settings/routes/owner/target,
body kind, navigation grid reference and script/graph path. Bound references compare by key.
Unbound external grid references retain their authored UUID. The signature is not a hash of every
asset or collider property: bump Schema when changing content that would make old progress unsafe.
Unknown format versions (0, 5 and later) are rejected. Base progress uses v1; [quest books](GameplayQuest.md) use v2,
[loadouts](GameplayLoadout.md) v3 and [combat](#combat-state-save-v4) v4. V1 restores into new quest content
with Locked/zero-progress books while preserving compatible base state.

Only scene roots without bone attachments can bind. Root scale must be nonsingular. Physical scale
must match at load because resizing physics shapes is a separate operation. No parent/hierarchy
changes or arbitrary components are reconstructed. An absent saved slot is represented by disabling
its live replacement. A present saved slot without a live replacement is rejected before mutation:
recreate/bind it first. This supports authored prefab pools and logical slots rather than arbitrary
ECS resurrection. The entire binding set is explicit; unrelated scene objects remain outside the save.

## Blueprint and native API

| Node | Result |
|---|---|
| `Gameplay.SaveBind` | Bind Target to Key; conflicting key/entity, foreign scene or child is rejected. |
| `Gameplay.SaveFind` | Pure lookup: Found, Entity and the entity's own Active flag. Inactive slots are still found. |
| `Gameplay.SaveUnbind` | Remove a key from the current binding set. Existing files then require matching content again. |
| `Gameplay.SaveSpawn` | Idempotent persistent prefab slot; returns Success, Entity and Reason. |
| `Gameplay.SaveWrite` | Capture compatible runtime state and atomically replace Slot. |
| `Gameplay.SaveLoad` | Validate and restore Slot, returning Recovered when backup supplied the loaded state. |
| `Gameplay.SaveInspect` | Validate file/backup without changing gameplay; this is not a content-compatibility check. |

Write, Load and Inspect return Success, Recovered, Entities and Reason. The scene shares a limit of
two file operations per frame, including failed attempts. Inspect is an execution node so merely
reading a graph value does not perform disk I/O. Bind/Find and native Capture/Restore do not use the
file budget. Saving is synchronous and bounded; asynchronous large-world/ECS saving remains separate.

Native `Capture(schema)` returns a GameSave; `EncodeGameSave` and `DecodeGameSave` are pure codecs.
`Restore(snapshot, schema)` validates native-supplied snapshots through the same codec. Exceptions
from Capture report invalid content; file/restore operations return SaveResult. Successful restore
reports the complete snapshot entity count, including absent slots.

## Saved state and commit

- Root position, rotation, scale and own active flag.
- Health/shield, dead/death counter, remaining immunity and respawn delay.
- Inventory items and remaining attack cooldown.
- Used interaction count and remaining interaction cooldown, preventing duplicate rewards.
- Opted-in quest modes/counts/receipts and XP, validated together with inventory in v2.
- AI mode, home/goal/last seen/facing, memory, wait, patrol direction/index, enabled flag,
  target key and cumulative gameplay counters.

Cooldowns are stored as remaining durations, then rebased to the destination rules clock. Elapsed
wall time while the application is closed does not spend immunity, waits or respawn delay. Runtime
configuration stays in the authored project rather than being replaced by save data.

Restore parses and validates the complete bounded document, checks all bindings/signatures/ranges
and prepares copies of rule and AI records before commit. An invalid late record cannot partly
change earlier health, bags, poses or AI. Commit installs prepared state and poses, synchronizes 2D
bodies, 3D bodies/controllers, cancels old paths/world queries/traversal tickets and clears velocities.
Graph motor/camera caches are invalidated so they do not retain pre-load movement. AI reacquires LOS
and replans routes on its next update; a saved terminal Blocked mode stays blocked until explicit
reset. Native recovery watchdogs, path handles and retry clocks are transient, not serialized.

Pending rule/AI event queues are cleared, not replayed. [Projectiles](GameplayCombat.md) in flight and
status effects are part of [save v4](#combat-state-save-v4). Active-state lifecycle callbacks run after
state installation under a reentry guard: they may perform normal gameplay effects, which are not
rolled back. Content should keep those callbacks idempotent. Arbitrary native callback exceptions
or process/allocation failure during commit are not a general engine transaction guarantee.

VM variables, global GameState, arbitrary native script memory, animation/physics simulation state,
child transforms, content mutations and complete streamed-world cell residency are outside this
snapshot. Use existing GameState/SaveData for separate settings and explicit quest variables.
The sample's AutoResume setting chooses its QA phase; native HP/bag/AI reads establish acceptance.
An attached streamed-world agent can restore progress, but its outstanding query/portal ticket is
always cancelled and the world itself is not reconstructed.

## Combat state (save v4)

A scene that uses [combat](GameplayCombat.md) (at least one projectile, area or effect definition, a status
effect or a projectile in flight) captures **version 4**; every other scene keeps writing v1/v2/v3 exactly as
before. V4 keeps the v2 Quests and v3 Loadout records and adds:

- `Effects` on every saved entity: at most 16 instances in application order (`Key`, `Source`, `Team`,
  `FriendlyFire`, `Stacks` 1–16, `Remaining` > 0, `Tick` = seconds to the next tick, `Magnitude`,
  `Sequence`), only on present, living health actors (death clears effects). Times are relative, so no clock
  is rebased: Poison saved 0.8 s before a tick ticks 0.8 s after the load, and closed-game time spends
  nothing. Loadout energy is saved as it is: a MaxEnergy buff is in the snapshot with its effect.
- a payload `Combat` block: `Signature` (the definitions' gameplay fields, `CombatWorld::Signature()`, at
  most 65,536 characters; visual Prefab/Pool excluded) and at most 128 `Projectiles`, each in its pool `Slot`
  with its handle `Generation`, Key, effect override, Owner/Target references, Team snapshot, double
  Position/Velocity/Muzzle (written exactly), Age, Travelled, Damage and Attack snapshots, PierceLeft, up to
  eight Hits references and Phase (0 flying, 1 detonating). An optional `FormerOwner` reference (written only
  when set, never together with Owner) names an owner that was already gone at capture.

References are save keys, `uuid:<n>` for an unbound authored entity, or empty. A source, owner or target that
is gone at capture, or does not resolve at load, becomes none: the effect keeps ticking with its team
snapshot but credits nobody, and the projectile flies ownerless. The ownerless projectile still keeps its
former owner's UUID (the unresolved `uuid:` Owner or `FormerOwner`) as an exclusion, exactly as before the
save, so a load never lets it hit that UUID, and its area keeps admitting by the team snapshot. Hits only
exclude targets, so an authored UUID is kept. V4 files written before `FormerOwner` existed still load: their
former owner is unknown (no exclusion), the team rules are unchanged.

Define combat in `Event.Start` before `SaveLoad`, as loadouts. Restore rejects a different definition set
with `Combat definitions changed`. It stages each bound present entity's effects against the current
definitions (`Invalid saved effects: ...`, the 1,024 scene instances included) **before** the loadout, so a
saved effect's MaxEnergy bonus bounds the saved energy, and every projectile against its definition
(`ValidateProjectileState`, a defined effect override of the right kind, 32 per owner, unique slots). An
invalid late record leaves every world untouched. Commit replaces the bound actors' effect books (unbound
actors keep theirs, as their health) and every projectile in flight (handles stay valid: same slot and
generation; other slots' generations advance), hides pooled visuals until they are reacquired, and clears
combat events, hit records and the fixed-step accumulator. The accumulator (less than one step) is not
saved, as physics does not save its own; with the accumulator at zero a restored scene continues exactly as
the original (`CombatSaveV4ContinuesExactlyInAFreshScene`).

Older saves migrate to "no effects and no projectiles in flight" for bound actors, with their existing
legacy rules. An ability's combat links extend its signature only when set, so unlinked abilities keep
v3-identical signatures. The golden files in `Tests/Fixtures/GameSaves` (`v1.json`, `v2.json`, `v3.json` and
a real LoadoutWorkshop phase-0 `loadout-v3.json`, all written before v4 existed) decode and re-encode
byte-identically, and the real one restores into the Loadout2D scene with and without combat. The largest
v4 document (256 entities x 16 effects with 64-character keys, 128 projectiles x 8 hits, a 65,536-character
signature) encodes to 1,524,542 bytes, within the 2 MiB limit.

## Files and recovery

Files live next to the current project's SaveData file under `gameplay-saves/<Slot>.json`.
Documents have a format tag, version, scene/schema, strict fields and payload checksum. Limits:
2 MiB, nesting depth 16, 256 entities, 64 items per bag and bounded strings/numbers. Duplicate JSON
keys, duplicate bindings, invalid vectors/counters, unknown fields and inconsistent death/AI data
are rejected. Decode failure leaves the caller's previous snapshot untouched.

The checksum detects accidental corruption; it is not authentication or encryption. Before replacing
a valid primary, Write atomically preserves it as `<Slot>.json.bak`. If backup writing fails, primary
is not replaced. An invalid primary is never promoted over a valid backup. Load falls back only when
the primary is missing or fails file/codec validation. A valid but incompatible primary does not
silently load an older backup. Recovered explicitly reports a successful fallback. Backup data is
the previous valid write, so its progress can be older. Files use the existing temporary-file/rename
helper; complete reads are guaranteed by atomic replacement, not power-loss durability or a
multi-process writer lock. Keep one writer for a profile.

## Verification

`GameSave*` native tests cover codec bounds (v4 fields included), the golden legacy files, transactional rejection, configuration compatibility,
timer rebasing, stable UUID remapping, physical restore, used interactions, bounded slot/spawn/I/O,
backup failures, lifecycle and streamed query/portal cancellation.

Run `python3 Tests/save_workflow_test.py --build build --exports` for actual 2D/3D Player input,
fresh-process resume, corrupt-primary recovery, Editor write/resume, exported write/resume and a
real resumed game after export relocation. `Tests/save_editor_test.py` exercises real ImGui
SaveBind/SaveWrite/SaveLoad add/save/Undo. Reports and screenshots: `build/GameplaySave/`.

Scenes with [equipment/abilities](GameplayLoadout.md) capture version 3, preserving energy,
equipment ownership, unlock flags, use counts and remaining cooldowns. Legacy v1/v2 loadouts
initialize from current authored defaults; existing compatibility checks remain in force.
