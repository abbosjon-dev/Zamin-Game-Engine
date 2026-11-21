# Gameplay combat: projectiles

`Gameplay::CombatWorld` adds bounded 2D/3D projectiles to the shared gameplay rules. A projectile is a
kinematic simulation record, not a physics body: it is stepped on the physics fixed step, hits health
actors through an analytic swept-sphere test, is stopped by world colliders through physics queries,
and changes health only through [RuleWorld](GameplayRules.md). Ownership, team rules, pierce, lifetime,
pooling and the optional visual prefab are deterministic and capped. Area damage, timed status effects
and the combat HUD/save integration build on this package (see [Later work](#later-work)).

## Ownership and definitions

`CombatWorld::For(scene)` stores one combat context in the scene's EnTT registry, next to the other
gameplay worlds. It is reset on Play start/stop and scene destruction, and Play stop destroys its pooled
visual entities. Access is main-thread only; cross-scene entities are rejected.

Definitions are shared per scene and keyed by an item-style key (1–64 letters, digits, `_ . -`). Up to 32
projectile definitions exist per scene. Defining a key again with an identical specification succeeds,
so every prefab instance may define on `Event.Start`; a different specification under the same key fails
with `Definition differs`. Definitions are immutable for the rest of the Play session. Defining requires
Play. Mutable combat state is runtime data: there are no new ECS components and nothing is written to
scene files.

| Field | Range | Meaning |
|---|---|---|
| Kind | 0 Damage, 1 Heal, 2 Shield | What the payload does to an admitted target. |
| Speed | 0.1–500 m/s | Launch speed. Gravity may change it during flight. |
| Lifetime | 0.02–30 s | Rounded up to the next fixed step. |
| MaxDistance | 0 (off) or 0.1–1000 m | Travelled path length; also rounded up to a step. |
| Radius / TargetRadius | 0–5 / 0–10 m, not both 0 | Projectile sphere and the radius added around each actor. |
| TargetOffset | length ≤ 100 | Aim point on owner and targets (as melee `Origin`). |
| Damage | 0–1,000,000 | Base payload; Heal/Shield amount for support projectiles. |
| Pierce | 0–7 | Extra targets after the first. |
| Gravity | each component ≤ 100 | Constant acceleration (z ignored in 2D). |
| Homing | 0–1440 °/s | Turn-rate cap toward the Fire target. |
| BlockMask | 0–65535 | World-collision layers that stop it; 0 = never blocked by the world. |
| TwoD, FriendlyFire, PierceShield, IgnoreGrace | flags | Plane mode and damage options. |
| Prefab, Pool | `.zprefab` inside Assets, 0–32 | Optional visual and its pooled instance count. |

`Effect`, `Area` and `DetonateOnExpire` are part of the definition format for the area/effect payloads.
Until those registries exist, a definition that names an effect or area is rejected
(`Projectile effect/area payloads are not available`) instead of silently doing nothing.

## Lifecycle

**Fire** (`CombatWorld::Fire`, `Gameplay.ProjectileFire`) requires Play and a live, active, living health
actor as owner. The aim point is `ownerPosition + TargetOffset`; `Origin` is a world-space launch offset
from it (length ≤ 100). A nonzero `Direction` is normalised; otherwise the projectile aims straight at the
target's aim point (no ballistic solve). In 2D the z components are zeroed and the projectile keeps the
launch depth. Outgoing damage is snapshotted at launch: `Damage < 0` means the definition's value, and the
owner's effective loadout Attack is added for the Damage kind. Armor, shield and hit grace apply at impact.
The owner's team is snapshotted too. Fire returns a handle `slot | generation << 8` (0 = none) and emits
`Fired` to the owner. Game conditions return a Reason; foreign entities, a non-finite or too long origin
and an out-of-range damage throw, as RuleWorld does.

**Flight.** `Scene::UpdateRuntime` calls `TickCombat` immediately after `StepPhysics`, inside the game-time
branch: editor pause, single step, UI pause and time scale behave exactly as physics. Combat keeps its own
accumulator over the scene's `PhysicsStep` (`h = 1 / StepRate`, at most `MaxStepsPerFrame` steps; a frame at
the cap drops the remaining time, as physics does). Each step applies homing (rotation toward the target by
at most `Homing · h`, speed kept; homing pauses while the target is gone or inactive), then semi-implicit
Euler `v += g·h, p += v·h` in double precision. The first step's segment starts at the owner's aim point and
passes through the launch point: a muzzle inside or behind a wall impacts that wall.

**End.** A projectile is released when its last admitted hit spends it, when a world blocker stops it
(`Impact`), when `Age ≥ Lifetime` or `Travelled ≥ MaxDistance` after a step (`Expired`), or by
`Cancel(handle)` (`Cancelled`). The whole final step is swept, so lifetime and distance round up to whole
steps. `Projectile(handle)` returns a snapshot; a stale handle (released, reused slot) returns nothing.

## Hits and ownership

Every step sweeps the full segment, so there is no tunnelling at low frame rates or high speeds.

- **Actor channel** (analytic, always on): the projectile sphere is swept against every living, active,
  health-configured RuleWorld actor, centred at `worldPosition + TargetOffset` with radius
  `Radius + TargetRadius` (an XY circle in 2D). Actors need no collider. Candidates are sorted by
  (segment T, UUID) and at most 16 are considered per step.
- **World channel** (physics queries against `BlockMask`, triggers excluded): 3D uses `SphereCast3DAll`
  (`Raycast3DAll` for radius 0); 2D uses `Raycast2DAll`, one ray for radius 0 or three parallel rays (centre
  and ±radius) otherwise. Hits on a health actor or on a pooled visual, or on their children up to 8 levels
  deep, are ignored: actors belong to the actor channel, so a character controller or collider actor is
  never hit twice. After the first step the query starts 2 cm behind the segment, so a step that ends on a
  surface cannot start the next query inside the collider (queries ignore colliders they start in).
- The first remaining world hit at segment T<sub>w</sub> is the blocker. Actor candidates with T ≥ T<sub>w</sub>
  are behind the wall and are not processed.

Payload admission (`Gameplay::Admits`):

| Payload | Target is the owner | Same nonzero team | Other team, or either team 0 |
|---|---|---|---|
| Damage | never | only with FriendlyFire | yes |
| Heal / Shield | never | yes | no (an owner on team 0 supports nobody else) |

- **One hit per target**: a projectile records up to 8 hit actors and never hits one twice.
- **Pierce**: admitted hits are processed in (T, UUID) order. Each applies the payload; after `Pierce + 1`
  hits the projectile is spent at that hit point.
- **Rejected hits still count**: a hit that applies nothing (hit grace, zero damage, a full-health heal) still
  consumes the shot and marks the target. A victim killed earlier in the same step (by a lower slot) is
  skipped and the projectile passes through. Inactive actors are not probed.
- **Ownerless projectiles**: owner and target are UUID plus generational handle, checked every step. If the
  owner is destroyed or recreated, the projectile keeps flying with its team and damage snapshot, gives no
  kill credit (no Source on the Damage event, no quest Kill notice) and emits no owner events. It still never
  hits the former owner's UUID. An owner that merely dies keeps its projectiles.
- Projectiles never interact with other projectiles.

Damage goes through `RuleWorld::Damage(victim, DamageRequest)` with `SourceTeam` = the launch team,
`SourceAttack = false` (Attack is already in the snapshot), the definition's PierceShield/IgnoreGrace
flags and `Cause` = the projectile key, which appears as the Damage/Died event `Item`. Support
projectiles call `RuleWorld::Heal(target, amount, shield, owner, key)`. Kill credit and quest Kill
notices work as for melee.

## Pooling and visuals

The logical pool is a fixed array of 128 slots: the lowest free slot is used first and its generation
increases on release, so old handles become stale. Step scratch buffers are reserved once; stepping makes
no per-shot or per-step allocation in the combat code (physics `*All` queries return vectors).

A definition with `Prefab` gets visual-only instances:

- The path is joined safely under the project's Assets folder and must name an existing `.zprefab`.
- Defining instantiates the first instance to validate it. The root and every descendant must not have a
  Rigidbody, collider, CharacterController3D, NativeScript (graphs) or NavAgent/NavObstacle; an invalid
  prefab is destroyed and the definition fails (logged as `Snaryad vizual prefabi rad etildi`).
- Instances are scene roots tagged `Projectile visual`, inactive while free and never bound or saved.
- After each frame's steps, a live projectile without a visual takes a free instance of its definition or,
  below `Pool` and the 256-per-scene cap, a new one (at most 8 instantiations per frame; later frames fill
  the pool). Without one it flies invisibly: gameplay never depends on visuals.
- The visual's transform is written once per frame: position; 2D rotation `atan2(vy, vx)`; 3D look rotation
  along the velocity. Released projectiles hide their visual; an externally destroyed visual is dropped.

## Events and hit records

`CombatWorld::Poll(actor)` / `Gameplay.CombatEvent` pops the actor's FIFO (32 events, oldest dropped,
saturating `Dropped`). Kinds: `Fired`, `Hit` (to the owner and the victim; Amount = applied), `Impact`,
`Expired`, `Cancelled`. Events carry Key, Source, Subject, Amount and the world Position. A 32-record hit
ring (`Recent(actor, outgoing, incoming, maxAge, out)`) keeps the newest hits for HUD feedback.
`Stats()` reports live/pooled/shown counts, steps/sweeps/actor tests of the last frame, total hits and the
dropped time.

## Blueprint

| Node | Inputs → outputs |
|---|---|
| `Gameplay.ProjectileDefine` | Projectile, Kind, Speed, Lifetime, MaxDistance, Radius, TargetRadius, TargetOffset, Damage, Pierce, Gravity, Homing, TwoD, FriendlyFire, PierceShield, IgnoreGrace, DetonateOnExpire, BlockMask, Effect, Area, Prefab, Pool → Success, Reason |
| `Gameplay.ProjectileFire` | Target (owner), Projectile, Origin, Direction, Victim, Damage (-1 = definition) → Success, Projectile (handle), Reason |
| `Gameplay.ProjectileState` (pure) | Projectile → Alive, Key, Position, Velocity, Age, Hits, Live, Capacity |
| `Gameplay.ProjectileCancel` | Projectile, Detonate → Success |
| `Gameplay.CombatEvent` | Target → Has, Kind, Key, Source, Subject, Amount, Position, Dropped |

Integer pins (Kind, Pierce, Pool, BlockMask, the handle) must be whole numbers in range; a wrong value
throws `Combat integer outside bounds`, as the loadout nodes do. An unknown Victim returns
`Projectile target unavailable`. Pure nodes do not mutate gameplay.

## Limits

| Resource | Limit | On overflow |
|---|---|---|
| Projectile definitions | 32 per scene | `Definition limit 32` |
| Live projectiles | 128 per scene | `Projectile limit 128`, nothing spent |
| Live projectiles per owner | 32 | `Owner projectile limit 32` |
| Successful Fire calls | 32 per scene frame | `Projectile spawn budget 32` |
| Hit set / pierce | 8 targets / 0–7 | structural |
| Actor candidates | 16 per step, sorted by (T, UUID) | further candidates ignored that step |
| World queries | 1 per live projectile per step (2D with radius: 3 rays) | structural |
| Visual instances | `Pool` 0–32 per definition, 256 per scene, 8 instantiations per frame | flies without a visual |
| Events / hit ring | 32 per actor / 32 per scene | oldest dropped |
| Fixed steps per frame | the scene's `MaxStepsPerFrame` (≤ 20) | remaining time dropped (`DroppedTime`) |

## Persistence

Projectiles in flight are not part of gameplay saves yet, and the save versions are unchanged (a combat
scene still writes v1/v2/v3 like any other). `SaveWorld::Restore` releases every live projectile, clears
combat events and hit records and resets the step accumulator before lifecycle callbacks, so no shot fired
before a load can hit the restored state (logged as `Saqlangan holat yuklandi: N ta uchayotgan snaryad
bekor qilindi`). Definitions stay; define them in `Event.Start` before loading, as loadouts.

## Determinism

All flight happens in fixed steps with total orderings (slots ascending, actors in UUID order, candidates
by (T, UUID)), budgets counted in call order, no RNG, no wall clock and no unordered iteration. With the
same inputs the event log is identical; with different frame rates the outcome is identical as long as no
frame hits the step cap. Physics queries are float; the conversion to double segment T is the only float
boundary. Hit grace still uses the rules clock (scaled frame time), so content that needs exact
frame-rate-independent grace windows should use zero grace for projectile victims.

## Native API

Include `Zamin/Gameplay/CombatWorld.h`. In a running scene: `CombatWorld::For(scene).DefineProjectile(spec,
error)`, then `Fire(FireRequest{ Key, Owner, Origin, Direction, Target, Damage })`, `Projectile(handle)`,
`Cancel(handle, detonate)`, `Poll(actor)`, `Recent(...)`, `Stats()`. `ProjectileModel.h` holds the pure,
scene-free parts (`ValidateProjectileSpec`, `ValidateProjectileState`, `AdvanceProjectile`, `SweepSphere`,
`SweepActors`, `Admits`, `ProjectilePool`) for custom tools and tests.

## Validation

`ProjectileModel*` tests cover specification and restore-state bounds, a 500 m/s projectile against a
0.05 m target at 20 Hz, start-inside and degenerate segments, 2D depth, (T, UUID) ordering and truncation,
owner/hit/team skipping, the admission matrix, homing turn cap and speed, the gravity arc, the muzzle segment
and pool generations. `CombatWorld*` tests use real scenes: shared-rule damage and events, Attack snapshot
with Armor at impact, support projectiles, ownerless/recreated owners, pierce order, absorbed and
pass-through hits, Box2D and Jolt walls, the muzzle-in-wall check, a face exactly on a step boundary,
collider and character-controller actors hit once, per-frame/owner/pool budgets, lifetime and distance
rounding, pause/time scale/step cap, Play stop/start, definitions, visual prefab validation and pooling,
the interim save policy, Blueprint dispatch, and determinism (identical event-log hash in fresh scenes;
identical outcomes at 1/30, 1/60, 1/144 s and a jittered frame sequence).

## Later work

Area damage/support, timed status effects and stacking, ability links, HUD feedback, save v4 with
projectiles in flight and the playable combat slice are the remaining items of this package. Out of
scope: projectile–projectile interaction, ricochet/bounce, knockback impulses, ballistic aim solving,
world-anchored damage numbers and network prediction/authority.
