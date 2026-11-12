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
Unknown format versions are rejected. The initial codec is version 1; no legacy migration is implied.

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

Pending rule/AI event queues are cleared, not replayed. Active-state lifecycle callbacks run after
state installation under a reentry guard: they may perform normal gameplay effects, which are not
rolled back. Content should keep those callbacks idempotent. Arbitrary native callback exceptions
or process/allocation failure during commit are not a general engine transaction guarantee.

VM variables, global GameState, arbitrary native script memory, animation/physics simulation state,
child transforms, content mutations and complete streamed-world cell residency are outside this
snapshot. Use existing GameState/SaveData for separate settings and explicit quest variables.
The sample's AutoResume setting chooses its QA phase; native HP/bag/AI reads establish acceptance.
An attached streamed-world agent can restore progress, but its outstanding query/portal ticket is
always cancelled and the world itself is not reconstructed.

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

`GameSave*` native tests cover codec bounds, transactional rejection, configuration compatibility,
timer rebasing, stable UUID remapping, physical restore, used interactions, bounded slot/spawn/I/O,
backup failures, lifecycle and streamed query/portal cancellation.

Run `python3 Tests/save_workflow_test.py --build build --exports` for actual 2D/3D Player input,
fresh-process resume, corrupt-primary recovery, Editor write/resume, exported write/resume and a
real resumed game after export relocation. `Tests/save_editor_test.py` exercises real ImGui
SaveBind/SaveWrite/SaveLoad add/save/Undo. Reports and screenshots: `build/GameplaySave/`.
