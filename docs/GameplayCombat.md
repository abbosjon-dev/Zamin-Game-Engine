# Gameplay combat: projectiles, areas and status effects

`Gameplay::CombatWorld` adds bounded 2D/3D projectiles, instant area damage/support and timed status
effects to the shared gameplay rules. A projectile is a kinematic simulation record, not a physics body:
it is stepped on the physics fixed step, hits health actors through an analytic swept-sphere test, is
stopped by world colliders through physics queries, and changes health only through
[RuleWorld](GameplayRules.md). Areas hit every admitted actor in a radius (with optional line of sight);
status effects tick, stack and expire on the same fixed step and modify loadout stats, movement speed and
the ability to act. Ownership, team rules, pierce, lifetime, stacking, pooling and the optional visual
prefab are deterministic and capped. Ability links, the combat HUD and save v4 build on this package (see
[Later work](#later-work)).

## Ownership and definitions

`CombatWorld::For(scene)` stores one combat context in the scene's EnTT registry, next to the other
gameplay worlds. It is reset on Play start/stop and scene destruction, and Play stop destroys its pooled
visual entities. Access is main-thread only; cross-scene entities are rejected.

Definitions are shared per scene and keyed by an item-style key (1–64 letters, digits, `_ . -`). Up to 32
projectile, 32 area and 32 effect definitions exist per scene. Defining a key again with an identical specification succeeds,
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

| Effect, Area | registry keys or empty | Status effect applied to every committed hit; area detonated where the projectile ends. |
| DetonateOnExpire | flag (requires Area) | Also detonate when lifetime/distance runs out. |

Payload keys are resolved when the projectile is fired, so definitions may be made in any order. Fire fails
without spending anything when a key is missing (`Projectile effect not defined`, `Projectile area not
defined`, `Area effect not defined`) or when an effect does not match its payload: damage carries hostile
effects, Heal/Shield carry beneficial ones (`Projectile effect kind mismatch`, `Area effect kind mismatch`).
Definitions are immutable for the Play session, so a resolved key still exists when the projectile hits.

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

**Detonation.** A projectile with an `Area` becomes *Detonating* instead of being released when its last
hit spends it, at a world impact, at expiry with `DetonateOnExpire`, or by `Cancel(handle, true)` (which
detonates on the next step where the projectile is). The area is centred on the end point: the hit point
(the projectile centre at contact, `Radius + TargetRadius` from the victim's aim point, so an area at least
that large also reaches the direct victim), the wall point or the last position. Its magnitude is the
area's Amount plus the owner Attack snapshotted at launch (Damage areas; `ProjectileState::Attack`). At most
16 projectiles detonate per fixed step, in slot order; the rest keep their slot and detonate on the next
step. An ownerless projectile detonates with its launch team, no Attack change and no kill credit, and its
former owner's UUID is still spared.

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

## Areas

`AreaSpec` / `Gameplay.AreaDefine` describes an instant area: a projectile detonation, a direct
`ApplyArea(key, source, center, magnitude)` / `Gameplay.AreaApply`, or (later) an ability.

| Field | Range | Meaning |
|---|---|---|
| Kind | 0 Damage, 1 Heal, 2 Shield | Damage, or support for allies. |
| Amount | 0–1,000,000 | Magnitude at the centre (`AreaApply` Magnitude ≥ 0 overrides it). |
| Radius | 0.05–50 m | Targets whose aim point is within the radius (XY distance in 2D). |
| Falloff, EdgeScale | 0 None / 1 Linear, 0–1 | Linear: scale `1 − (1 − EdgeScale) · d / Radius`. |
| MaxTargets | 1–32 | Nearest admitted, visible targets. |
| TargetOffset | length ≤ 100 | Aim point on each target (`worldPosition + TargetOffset`). |
| LineOfSight, Mask | flag, 0–65535 | A ray from the centre to each target point against the mask. |
| AffectOwner, FriendlyFire, PierceShield, IgnoreGrace | flags | Owner and team rules, damage options. |
| TwoD | flag | XY distance and Box2D rays; otherwise 3D distance and Jolt rays. |
| Effect | effect key or empty | Status effect applied to every affected target that survives. |

Application: every living, active health actor within the radius that passes the payload admission (the
table under [Hits and ownership](#hits-and-ownership), with `AffectOwner` deciding the owner) is a
candidate. Candidates are sorted by (distance, UUID). Without line of sight the nearest `MaxTargets` are
affected; with it, the nearest 32 candidates get one ray each (at most 32 rays per area) and the first
`MaxTargets` visible ones are affected. Blockers follow `RuleWorld` LOS: a hit counts only before the
target point, and health actors (with their collider children) and pooled visuals never occlude. Each
target receives `magnitude × scale`:
Damage through `RuleWorld::Damage` (`Cause` = area key, the source's team, `SourceAttack = false` because
Attack is already in the magnitude), Heal/Shield through `RuleWorld::Heal(target, amount, shield, source,
key)`, then the area's effect. The source receives `Detonated` (Amount = affected count, Position = centre);
each target receives `Hit` and a hit record. `AreaResult` reports `Affected`, the total `Applied` and a
Reason.

A direct `ApplyArea` needs Play and at most 16 calls per scene frame (`Area budget 16`). An optional source
must be active, alive and not stunned (`Area source inactive`, `Area source dead`, `Stunned`); its Attack
joins Damage areas *before* the falloff, exactly like a projectile's launch snapshot. Without a source the
area is environmental: it ignores teams and has no owner to spare. `Gameplay.AreaApply` centres on
`Victim + TargetOffset` when a Victim is set, otherwise on `Center`. With debug ray drawing enabled each
area draws a circle (2D) or wire sphere (3D) for 0.5 s.

## Status effects

`EffectSpec` / `Gameplay.EffectDefine` describes a timed status effect on a health actor.

| Field | Range | Meaning |
|---|---|---|
| Duration | 0.05–600 s | Lifetime of one application. |
| Period, Tick, TickAmount | 0 or 0.05–min(60, Duration) s; 0 None, 1 Damage, 2 Heal, 3 Shield; 0–100,000 | Periodic damage/heal/shield. Tick None ⇔ Period 0 and TickAmount 0. |
| Modifiers | 5 numbers, −10,000–10,000 | Additive bonus to Attack, Armor, Power, MaxEnergy, Regen. |
| Speed | 0–4 | Movement scale: < 1 slow, > 1 haste, 0 root. |
| Stun | flag | The actor cannot act (see [Modifiers](#modifiers-stats-speed-and-stun)). |
| Hostile | flag | Damage ticks and Stun require it; Heal/Shield ticks forbid it. |
| PierceShield | flag | Damage ticks bypass the shield. |
| Stacking, MaxStacks | 0 Refresh, 1 Extend, 2 Independent, 3 HighestWins; 1–16 (HighestWins: 1) | Re-application rule. |

`ApplyEffect(target, key, source, magnitude)` / `Gameplay.EffectApply` applies it; `magnitude < 0` uses
TickAmount (0–100,000 otherwise). The target must be a living, active health actor (`Effect target health
not configured`, `Effect target inactive`, `Effect target dead`). With a source the team rule applies:
hostile effects never land on the source's allies and beneficial ones only on allies (a source on team 0
supports only itself), `Effect target not allowed`; an explicit self target and a source-less
(environment) application are always admitted. Projectile and area payloads apply their effect after each
committed hit on a target that survived it (an absorbed or zero-damage hit included), with the payload's
team and FriendlyFire. At most 64 direct applications per scene frame (`Effect budget 64`).

| Rule | Re-applying a key that is active | Ticks and modifiers use |
|---|---|---|
| Refresh | Stacks `min(MaxStacks, +1)` (`Stacked`, or `Refreshed` at the cap); remaining time resets to Duration; magnitude = max(old, new); the latest applier becomes the source. The tick phase is kept, so frequent refreshes cannot postpone ticks. | magnitude × stacks, modifiers × stacks |
| Extend | One instance: remaining += Duration, capped at Duration × MaxStacks (`Extended`). | × 1 |
| Independent | A new instance while fewer than MaxStacks exist (`Added`); otherwise the instance with the least time left (ties: the older one) is replaced (`Replaced`). Each instance has its own timer and tick phase. | each × 1, summed |
| HighestWins | Stronger magnitude replaces (`Replaced`, new timer and source); equal extends to `max(remaining, Duration)` (`Refreshed`); weaker is rejected (`Stronger effect active`). | × 1 |

A book holds at most 16 instances per actor: a new key on a full book is rejected (`Effect limit 16`) and
other keys are never evicted. A scene holds at most 1024 instances (`Scene effect limit 1024`) and 512 actor
books; a re-application that adds no instance still works at the cap. Rejected applications change
nothing.

**Ticking and expiry.** Once per fixed step, before projectiles move, every book is stepped in UUID order:
remaining time and the time to the next tick decrease by `h`; each due tick fires (at most 4 per instance
per step; carried ticks fire on later steps) and the instance expires when its remaining time reaches 0
(both with a 1e-9 s epsilon for the float accumulation of steps such as 1/60 s). A tick due on the expiry
step still fires, so a 3 s effect with a 1 s period ticks exactly 3 times at 1, 2 and 3 s at any step
rate from 20 to 240 Hz, and expiry rounds up to a whole step. Ticks apply in instance order:
Damage through `RuleWorld::Damage` with `IgnoreGrace = true` and `StartGrace = false` (ticks are not
stopped by hit grace and grant none), the applier's team snapshot, `SourceAttack = false` (only the
victim's Armor applies), PierceShield from the definition and `Cause` = effect key; Heal/Shield through
`RuleWorld::Heal`. The live applier gets kill credit and quest Kill notices; a destroyed or recreated
applier gives none, and its ticks keep its team. Each tick writes a hit record with `Periodic = true`.

**Lifecycle.** Death removes all of an actor's effects at once (`EffectCleared`, Amount = count), so a
damage-over-time kill drops that actor's remaining ticks of the step and a respawned actor starts clean.
Inactive actors keep their effects frozen: no ticks, no expiry. Editor pause, time scale 0 and the step cap
freeze effects exactly as projectiles. `RemoveEffects(actor, key)` / `Gameplay.EffectRemove` removes every
instance of one key, or all with an empty key (`EffectRemoved`). Events (to the target): `EffectApplied`
(Added/Replaced, Amount = stacks), `EffectRefreshed` (Refreshed/Stacked/Extended), `EffectExpired`,
`EffectRemoved`, `EffectCleared`.

`Effect(actor, key)` / `Gameplay.EffectState` reports Active, Stacks (Refresh: stacks; otherwise the number
of instances), Instances, the longest Remaining, the duration cap (`Fraction = Remaining / cap`) and the
total per-tick Magnitude. `Effects(actor, out)` copies the instances for tools and inspectors.

## Modifiers: stats, speed and stun

`Modifiers(actor)` / `Gameplay.CombatModifiers` composes an actor's effects: the stat bonus is the sum of
every instance's Modifiers (× stacks for Refresh), Speed is the strongest slow (< 1) times the strongest
haste (> 1), clamped to 0–4, and Stunned is any Stun instance. It is recomposed on every change.

- **Loadout stats.** The bonus enters `EffectiveStats` after equipment and before the clamp, so
  `LoadoutWorld::State` (and the HUD), outgoing Attack, Armor, Cast Power, energy capacity and regeneration
  all use buffed values. Actors without a loadout get Attack and Armor from their effects alone (base 0).
  Projectiles snapshot the buffed Attack at launch. When a MaxEnergy bonus ends, energy is clamped to the
  new capacity at once; no energy is granted back.
- **Speed.** `Gameplay.Motor2D/3D` multiply the authored Speed by the scale (at most 1000); `AIWorld` sets
  the agent's navigation speed to `Brain.Speed × scale` every tick.
- **Stun.** A stunned actor's motors stop (as for a dead actor), its AI brain is not stepped (Status
  `Stunned`, timers and memory frozen), and `RuleWorld::Attack`/`Strike`/`Interact`, `LoadoutWorld::Cast`/
  `Equip`/`Unequip`, `CombatWorld::Fire` and a sourced `ApplyArea` return `Stunned` before spending any
  cooldown, energy or item. `Gameplay.AbilityState` reports `Ready = false`. Refills and effects applied
  *to* the actor still work.

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
saturating `Dropped`). Kinds: `Fired`, `Hit` (to the owner and the victim of a projectile, to the victim of
an area; Amount = applied), `Impact`, `Expired`, `Detonated` (to the area source; Amount = affected),
`Cancelled`, and the effect events `EffectApplied`, `EffectRefreshed`, `EffectExpired`, `EffectRemoved`,
`EffectCleared` (to the target). Events carry Key, Source, Subject, Amount and the world Position. A
32-record hit ring (`Recent(actor, outgoing, incoming, maxAge, out)`) keeps the newest hits, area hits and
damage/heal-over-time ticks (`Periodic`) for HUD feedback. `Stats()` reports live/pooled/shown counts,
steps/sweeps/actor tests/effect ticks/area rays of the last frame, total hits and area applications
(`Detonations`), live effect instances and the dropped time.

## Blueprint

| Node | Inputs → outputs |
|---|---|
| `Gameplay.ProjectileDefine` | Projectile, Kind, Speed, Lifetime, MaxDistance, Radius, TargetRadius, TargetOffset, Damage, Pierce, Gravity, Homing, TwoD, FriendlyFire, PierceShield, IgnoreGrace, DetonateOnExpire, BlockMask, Effect, Area, Prefab, Pool → Success, Reason |
| `Gameplay.ProjectileFire` | Target (owner), Projectile, Origin, Direction, Victim, Damage (-1 = definition) → Success, Projectile (handle), Reason |
| `Gameplay.ProjectileState` (pure) | Projectile → Alive, Key, Position, Velocity, Age, Hits, Live, Capacity |
| `Gameplay.ProjectileCancel` | Projectile, Detonate → Success |
| `Gameplay.CombatEvent` | Target → Has, Kind, Key, Source, Subject, Amount, Position, Dropped |
| `Gameplay.AreaDefine` | Area, Kind, Amount, Radius, Falloff, EdgeScale, MaxTargets, TargetOffset, TwoD, LineOfSight, Mask, AffectOwner, FriendlyFire, PierceShield, IgnoreGrace, Effect → Success, Reason |
| `Gameplay.AreaApply` | Target (source), Area, Center, Victim (centre on it when set), Magnitude (-1 = Amount) → Success, Affected, Applied, Reason |
| `Gameplay.EffectDefine` | Effect, Duration, Period, Tick, TickAmount, Modifiers (5 numbers), Speed, Stun, Hostile, PierceShield, Stacking, MaxStacks → Success, Reason |
| `Gameplay.EffectApply` | Target, Source (0 = environment), Effect, Magnitude (-1 = TickAmount) → Success, Outcome, Stacks, Remaining, Reason |
| `Gameplay.EffectRemove` | Target, Effect ("" = all) → Removed |
| `Gameplay.EffectState` (pure) | Target, Effect → Active, Stacks, Instances, Remaining, Fraction, Magnitude |
| `Gameplay.CombatModifiers` (pure) | Target → Stunned, Speed, Attack, Armor, Power, Maximum, Regen, Effects |

Integer pins (Kind, Pierce, Pool, BlockMask, Falloff, MaxTargets, Mask, Tick, Stacking, MaxStacks, the
handle) must be whole numbers in range; a wrong value throws `Combat integer outside bounds`, as the
loadout nodes do. Modifiers needs exactly five numbers. An unknown Victim or Source returns `Projectile
target unavailable`, `Area victim unavailable` or `Effect source unavailable`. Outcome is one of `Added`,
`Refreshed`, `Stacked`, `Extended`, `Replaced`, `Rejected`. Pure nodes do not mutate gameplay.

## Limits

| Resource | Limit | On overflow |
|---|---|---|
| Projectile / area / effect definitions | 32 / 32 / 32 per scene | `Definition limit 32` |
| Live projectiles | 128 per scene | `Projectile limit 128`, nothing spent |
| Live projectiles per owner | 32 | `Owner projectile limit 32` |
| Successful Fire calls | 32 per scene frame | `Projectile spawn budget 32` |
| Hit set / pierce | 8 targets / 0–7 | structural |
| Actor candidates | 16 per step, sorted by (T, UUID) | further candidates ignored that step |
| World queries | 1 per live projectile per step (2D with radius: 3 rays) | structural |
| Visual instances | `Pool` 0–32 per definition, 256 per scene, 8 instantiations per frame | flies without a visual |
| Events / hit ring | 32 per actor / 32 per scene | oldest dropped |
| Detonations | 16 per fixed step | the rest detonate on the next step, by slot |
| Direct `ApplyArea` calls | 16 per scene frame | `Area budget 16` |
| Area targets / line-of-sight rays | `MaxTargets` ≤ 32 / 32 per area | nearest first by (distance, UUID) |
| Effect instances | 16 per actor, 1024 per scene, 512 actor books | `Effect limit 16`, `Scene effect limit 1024`, `Combat actor limit 512` (no eviction) |
| Stacks / instances per key | MaxStacks 1–16 | rule-specific (see the stacking table) |
| Ticks | 4 per instance per fixed step | carried to later steps |
| Direct `ApplyEffect` calls | 64 per scene frame | `Effect budget 64` |
| Fixed steps per frame | the scene's `MaxStepsPerFrame` (≤ 20) | remaining time dropped (`DroppedTime`) |

## Persistence

Projectiles in flight and status effects are not part of gameplay saves yet, and the save versions are
unchanged (a combat scene still writes v1/v2/v3 like any other). `SaveWorld::Restore` releases every live
projectile, removes every status effect, clears combat events and hit records and resets the step
accumulator before lifecycle callbacks, so no shot fired or effect applied before a load can act on the
restored state (logged as `Saqlangan holat yuklandi: N ta uchayotgan snaryad bekor qilindi, M ta effekt olib
tashlandi`). Because a restore has no effects, `Capture` saves loadout energy at most at the capacity
*without* effect bonuses (a MaxEnergy buff's extra energy is not saved), so every snapshot is valid without
effects; without effects the saved bytes are unchanged. Definitions stay; define them in `Event.Start`
before loading, as loadouts.

## Determinism

All flight, ticking and expiry happen in fixed steps with total orderings (effects before projectiles;
books and probes in UUID order; instances, ticks and replacement ties in application Sequence order;
slots ascending; candidates by (T, UUID); area targets by (distance, UUID)), budgets counted in call order,
no RNG, no wall clock and no unordered iteration. With the
same inputs the event log is identical; with different frame rates the outcome is identical as long as no
frame hits the step cap. Physics queries are float; the conversion to double segment T is the only float
boundary. Hit grace still uses the rules clock (scaled frame time), so content that needs exact
frame-rate-independent grace windows should use zero grace for projectile victims.

## Native API

Include `Zamin/Gameplay/CombatWorld.h`. In a running scene: `CombatWorld::For(scene).DefineProjectile(spec,
error)`, then `Fire(FireRequest{ Key, Owner, Origin, Direction, Target, Damage })`, `Projectile(handle)`,
`Cancel(handle, detonate)`, `Poll(actor)`, `Recent(...)`, `Stats()`; `DefineArea`, `ApplyArea(key, source,
center, magnitude)`; `DefineEffect`, `ApplyEffect(target, key, source, magnitude)`, `RemoveEffects`,
`Effect`, `Effects`, `Modifiers`. The free functions `CombatStatBonus`, `CombatSpeedScale` and
`CombatStunned` are neutral without a combat world, and `OnCombatDeath` is called by RuleWorld on a kill.
`ProjectileModel.h` holds the pure, scene-free projectile parts (`ValidateProjectileSpec`,
`ValidateProjectileState`, `AdvanceProjectile`, `SweepSphere`, `SweepActors`, `Admits`, `ProjectilePool`)
and `EffectModel.h` the area and effect parts (`ValidateAreaSpec`, `AreaScale`, `OrderAreaTargets`,
`ValidateEffectSpec`, `ApplyEffect`, `TickEffects`, `ComposeModifiers`, `RemoveEffects`,
`ValidateEffectBook`) for custom tools and tests.

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

`EffectModel*` and `AreaModel*` tests cover the specification bounds, every stacking rule (the Refresh tick
phase, the Extend cap, Independent eviction order, HighestWins stronger/equal/weaker), book capacity
without eviction, exact tick counts for six duration/period pairs at 20, 30, 50, 60, 120, 144 and 240 Hz,
the 4-tick cap, modifier composition, book validation, falloff and target ordering. `CombatEffect*`,
`CombatArea*` and the detonation tests use real scenes: definitions and late key resolution, stacking
through the world with events, damage-over-time without grace and with quest kill credit, ownerless ticks,
death clearing, inactive/pause/time-scale freezing, admission/budget/1024-instance limits, loadout stat
composition and the energy clamp, stun gates that spend nothing, slow/haste/stun on Motor2D and AI
navigation, area falloff/ordering/teams/support, Box2D and Jolt line of sight, impact/expiry/cancel/wall
detonations with launch snapshots, the 16-per-step detonation budget, Blueprint nodes, the interim save
policy and a determinism scenario with every path (identical hash in fresh scenes and at 1/30, 1/60, 1/144 s
and a jittered sequence).

## Later work

Ability links (projectile/area/effect abilities), editor pin choices, HUD feedback, save v4 with projectiles
in flight and active effects, and the playable combat slice are the remaining items of this package. Out of
scope: projectile–projectile interaction, ricochet/bounce, knockback impulses, ballistic aim solving,
world-anchored damage numbers and network prediction/authority.
