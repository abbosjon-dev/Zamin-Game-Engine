# Zamin Engine — a plan of 500 tasks

The original milestones M01–M25 hold 20 tasks each. `[x]` — done, `[ ]` — planned.
The new 2D/3D product phases below are tracked separately.

## Active direction: a professional 2D and 3D engine

2026-11-09: the goal is a general-purpose engine where 2D and 3D are both first-class. Unreal-level breadth,
reliability and ease of development are the long-term target; no claim is made that this parity has been reached.
The phases below are run with a product-ready result and an acceptance criterion each. The original 500 tasks further
down are kept; a mark on them does not replace evidence that a new capability works.

| Phase | Result | Completion criterion | Status |
|---|---|---|---|
| P0 Reliable foundation | Atomic save, recovery, safe build, bounded Undo, CPU profile, separated runtime | No data lost on a save error, a malicious folder name is not executed, a Player without UI, an SSE2 test | The 7 audit items are done; baseline evidence 645 unit / 12 render / 265 editor steps |
| P1 Large-map 2D/3D rendering foundation | Terrain patch LOD, crack-free neighbour borders, partial GPU mesh updates; hierarchical tilemap culling | Geometry reduction, LOD transitions/hysteresis, collision accuracy, 2D visibility and draw order matching an oracle | Done (2026-11-10): 656 unit / 12 render / 265 editor steps; measurements below |
| P2 Content quality and authoring | Terrain GPU layers/PBR, normal/roughness, atlas/cook, foliage, 2D palette/animation convenience | 2D/3D scenes with real assets, import/export/undo roundtrip, a texture memory budget | GPU terrain v1 finished; the remaining content work is planned |
| P3 World and asset streaming | Disk cells, async load/unload, data layers, HLOD, floating origin, asset residency | No objects lost during fast movement/cancel/reload, bounded RAM/VRAM, no heavy IO on the main thread | Runtime v2: LRU cleanup, observed pressure gate, terrain tiles and grid mosaic done; HLOD/floating origin/hard timing planned |
| P4 The next renderer generation | GPU timer v1 exists; render graph, Vulkan, temporal AA/upscaling, large-scene visibility and lighting | A driver/physical GPU matrix, HDR/alpha/picking/shadow regressions, a reference hardware budget | Planned; virtual geometry and dynamic GI — separate research |
| P5 Gameplay, animation, physics and UI | Bringing the existing 2D/3D systems together at production level, large content and gameplay workflows | Two finished vertical slices, save/load, controller, animation/physics stress, UI input/scale tests | Motor/combat/AI/save/UI, quest, equipment/abilities, projectiles/effects and the Animator 1D/2D chain finished (2026-11-22); with two complete vertical slices (Lantern Keeper 2D, Beacon Pass 3D) the stress, UI input/scale, fresh-process resume, relocated export and profiling gates are done too (2026-11-22) |
| P6 Networking and extensibility | Server authority, replication, latency compensation, plugin SDK/ABI, safe hot reload | Tests for lost packets, latency, reconnect, version mismatch and plugin unload | Planned |
| P7 Platform, release and product | Linux/Windows, then macOS; cook/package/update/crash telemetry; one auth for editor/launcher | Install/export/run on a clean machine, requirements and licences, a signed release and a rollback test | Planned |
| P8 Final quality gate | Documentation, a migration path, two sample games and profiling reports | Long sessions, resource leaks, recovery/fuzz, a stable frame time in the reference scenes and usability tests | Planned |

### Finished section — quest/progression

- [x] Bounded objectives, prerequisites, receipts and progression model.
- [x] Native/Blueprint automatic gameplay notices, atomic bag/XP rewards and HUD.
- [x] Save v2 quest snapshots, v1 migration and transactional restore.
- [x] 2D/3D playable quests, fresh-process resume, Editor/export/screenshots.
- [x] Unit, sanitizer, regression, documentation and main push.

Evidence (2026-11-12): 1199/1199 unit (39 new), 11/11 headless CTest, 12/12 render.
Pure QuestModel ASan/UBSan/LSan: 12/12. An owner probe of 7 instrumented TUs (QuestModel/World/Graph, SaveModel/World,
GameplayRules/Runtime): 39/39, with LeakSanitizer.
The rest of the shared engine, Scene/serializer/physics/VM, is a normal build; this is not a sanitizer run of the whole engine.
Clang 18 syntax-only -Werror: 12 TUs. Real ImGui Configure/Claim/BindHUD add/save/Undo.
The three-phase acceptance/resume/no-replay of the 2D/3D Player and Editor and the compiler-free export's
write/resume/real relocated no-replay course: 14/14 workflows. Canonical state: XP=200,
Coin=5, Badge=1, two quests Claimed. The generator is deterministic; Python/format/docs checked.
The atlas readiness condition of the WorldCrowdHeadOn test was tightened from the first cell to all 300 cells;
10/10 independent runs passed. Reports and pictures: `build-review/GameplayQuest/`.
Software Mesa; the physical GPU budget was not measured. [Limits](GameplayQuest.md).

### Finished section — equipment, stats and abilities

- [x] Item definitions, 4 equipment slots, deterministic stat modifiers and bag capacity rollback.
- [x] Damage/heal/shield abilities, ingredients, energy/regen, cooldowns, unlocks and the Blueprint API.
- [x] Canonical-state HUD, v3 save/migration, transactional restore and actor lifecycle.
- [x] 2D/3D acceptance, fresh-process resume, relocated export and main push.

Evidence: 1225/1225 unit (26 new), 11/11 headless CTest. Pure LoadoutModel/RulesModel
ASan/UBSan/LSan: 6/6. An owner probe of 3 instrumented TUs (LoadoutModel/World/Graph):
26/26, with LeakSanitizer; the remaining SaveWorld/Scene/VM/physics/shared engine is a normal build.
This is not a sanitizer run of the whole engine. Clang 18 syntax-only -Werror: 7 TUs.
Real ImGui LoadoutConfigure/EquipmentEquip/AbilityCast add/save/Undo passed.
The acceptance/resume/no-replay of the 2D/3D Player and Editor and the compiler-free export's
write/resume/real relocated no-replay course: 14/14 workflows. Canonical state:
Sword/Plate equipped, Attack=20, Armor=100, Power=10, Energy=40, Bolt Uses=2,
Potion=1, Coin=5, XP=150; the saved first-cast cooldown is positive. The generator is deterministic;
Python/format/docs checked. Software Mesa, the hardware GPU budget was not measured.
Since the renderer did not change, the old 12 render cases were not run again.
[API and limits](GameplayLoadout.md). Reports: `build-review/GameplayLoadout/`.

### Finished section — projectiles and gameplay effects

- [x] Bounded 2D/3D projectile lifecycle, collision/hit ownership, pooling and lifetime.
- [x] Area damage/support, timed status effects and stacking/expiration rules.
- [x] Ability/effect authoring, HUD feedback and persistence compatibility.
- [x] A playable 2D/3D combat slice, profiling, export and regression gates.

Evidence (item 1, 2026-11-21): 1257/1257 unit (32 new: 12 `ProjectileModel*`, 20 `CombatWorld*`/`CombatRules*`),
11/11 headless CTest (with DocsCheck/DocsSnippets). Pure ProjectileModel/RulesModel ASan/UBSan/LSan: 32/32.
An owner probe of 7 instrumented TUs (CombatWorld, CombatGraph, ProjectileModel, GameplayRules, RulesModel, LoadoutWorld
and SaveWorld): 33/33, with LeakSanitizer (32 tests + 1 scratch test that drives the step/sweep/visual path in the
instrumented copy, because the Scene hooks inside libZamin are bound with `-Bsymbolic-functions`);
Scene/serializer/physics/VM and the rest of the shared engine are a normal build — this is not a sanitizer run of the whole engine.
Clang 18 syntax-only -Werror: 10 TUs. Mutation check: removing the actor filter of the world channel fails 3 tests,
reverting the wall fix at the end of a segment (T = 1) fails the new boundary test, removing the dead/inactive skip fails
1 test — this check found a bug where a projectile passed through a wall at the Box2D float boundary, and it was fixed.
Determinism: the same event-log hash in two fresh scenes; the same result at 1/30, 1/60, 1/144 s and with jittered
frames (7 hits, everything finishes within 3 s). A 500 m/s projectile at 20 Hz does not miss a 0.05 m target.
LoadoutWorkshop Player workflow (2D/3D × 3 phases): 6/6, the save is still v3. In a scratch demo the real Player and
Editor Play: 11/128 projectiles in flight with pooled visuals, no GL/graph errors. The interim save
policy: Restore cancels projectiles in flight, the save version did not change. Since the renderer and the editor UI
did not change, the render reference and the ImGui editor tests were not run; the Release bench is in item 4.
[API and limits](GameplayCombat.md).

Evidence (item 2, 2026-11-21): 1288/1288 unit (31 new: 11 `EffectModel*`/`AreaModel*`, 20 `CombatEffect*`,
`CombatArea*`, `CombatProjectile*`, `CombatDetonation*`), 11/11 headless CTest, `ctest -L docs` 2/2. Pure
EffectModel/ProjectileModel/RulesModel/LoadoutModel ASan/UBSan/LSan: 49/49. An owner probe of 9 instrumented TUs
(CombatWorld, CombatGraph, EffectModel, ProjectileModel, GameplayRules, RulesModel, LoadoutWorld, LoadoutModel and
SaveWorld): 84/84, with LeakSanitizer (83 tests + 1 scratch test that drives the effect phase, area LoS and cleanup on
death through `CombatWorld::Tick` in the instrumented copy); Scene/serializer/physics/VM
and the rest of the shared engine are a normal build — this is not a sanitizer run of the whole engine. Clang 18
syntax-only -Werror: 15 TUs (12 engine + 3 test). Mutation check: restarting the tick phase on Refresh, not clearing
effects on death, treating health actors as obstacles in LoS, spending the attack cooldown during a stun and removing
the 16/step detonation budget — each failed the intended tests. Tick accuracy: 6 duration/period pairs × 20, 30, 50,
60, 120, 144, 240 Hz (42 cases) — the tick count and the expiry step are exact (3 s/1 s
= 3 ticks). Determinism: a scenario with every effect/area path gives the same hash in two fresh scenes and at 1/30,
1/60, 1/144 s and with jittered frames; everything finishes within 7 s (0 projectiles, 0 effects, the ally at 91 HP =
70 + Mend 5 + 4 × Regen 4). A scratch 2D demo (Player 76 frames, Editor Play 90 frames, 30 fps): Sentry from 400 to
319 HP — matches 2 bombs × (8 + Attack 10) + Nova (12 + 10) × 0.894 falloff + 3 Poison ticks × 2; the actor behind
the wall 60/60 (LoS), Frost Extend capped at 6 s, stun 4 s, pooled visuals in the Editor hierarchy, no GL/graph
errors. Regression (Player): LoadoutWorkshop 6/6 (the save still v3), SaveWorkshop 6/6, AIWorkshop 2/2,
RulesWorkshop 2/2. The interim save policy: effects are not saved, Restore removes them; Capture writes the energy up
to the capacity without the effect bonus (byte-for-byte unchanged with no effects), the save version did not change.
Since the renderer did not change and no editor UI was added, the render reference, the ImGui editor tests and the
Editor workflow phases were not run; the Release bench is in item 4. [Areas and effects](GameplayCombat.md#areas).

Evidence (item 3, 2026-11-21): 1307/1307 unit (19 new: 2 `GameSaveLegacy*`, 3 `GameSaveModelV4*`, 6 `CombatSave*`,
4 `CombatAbility*`, 2 `CombatHUD*`, `GameUIModelCombatRowsRoundAndFade`, `VisualGraphPinChoicesAreTranslated`;
the 2 tests that checked the interim save policy were moved to the v4 semantics, the `Version 4` rejection test now
rejects 5 and 0), 11/11 headless CTest, `ctest -L docs` 2/2. First, on the unchanged tree, v1/v2/v3 and a real
LoadoutWorkshop phase-0 `loadout-v3.json` were written to `Tests/Fixtures/GameSaves`: all four re-encode byte for
byte; the real file restores into the headless Loadout2D scene both with and without combat (Sword/Plate, Energy 65,
Bolt Uses 1), and in a fresh Player process phase 1 gives `LOADOUT_RESUMED casts=2 xp=150 coin=5`, the rewritten save
still v3. The largest save v4 document (256 objects × 16 effects, 64-character keys, 128 projectiles × 8 hits, a
65 536-character signature) is 1 524 542 bytes — within 2 MiB; each of 38 struct and 12 JSON corruptions was rejected.
Determinism (c): after saving, restoring into a fresh scene and 15 + 150 steps, the projectile position/velocity/age,
the health and the effect rows (remaining time, tick phase, Sequence) are exactly equal. Pure ProjectileModel/EffectModel/
RulesModel/LoadoutModel/SaveModel/QuestModel/UIModel ASan/UBSan/LSan: 85/85. An owner probe of 14 instrumented TUs
(CombatWorld, CombatGraph, ProjectileModel, EffectModel, SaveModel, SaveWorld, LoadoutWorld, LoadoutModel,
LoadoutGraph, GameplayRules, RulesModel, GameplayUI, GameplayUIGraph and UIModel): 94/94, with LeakSanitizer (93
tests + 1 scratch test that drives the HUD/feed refresh, `CombatWorld::Tick` and the v4 Capture/Restore in the instrumented
copy; the ODR check is off because of definitions deliberately duplicated with libZamin); Scene/serializer/physics/VM
and the rest of the shared engine are a normal build — this is not a sanitizer run of the whole engine. Clang 18
syntax-only -Werror: 18 TUs (13 engine/editor + 5 test). Mutation check: staging effects after the loadout, adding a
tail to an unbound ability signature too, counting effect ticks in the hit marker, spending an area ability only when
Applied > 0, dropping the restored projectile generation to 1 and keeping the effects of a bound actor in an old save —
each failed the intended tests (the projectile commit flag mutation turned out equivalent and was not counted).
Regression (Player): LoadoutWorkshop 6/6 (the save still v3), SaveWorkshop 6/6 (v1), QuestWorkshop 6/6
(v2), UIWorkshop 6/6. A scratch 2D demo (30 fps): in phase 0 the Player wrote v4 at 1.2 s — 3 projectiles in flight,
Sentry 370/400 (400 − (12 + Power 10 + Attack 5) − 3 Poison ticks), Dummy 93/120 (pierce), 3 effects; in a fresh process
phase 1 `Combat state restored: 3 effects, 3 projectiles`, the restored Bolts hit (at frame 10 Sentry 339/400, BURN
x2/3, POISON x1/3). HUD Kind 5/6 and the 4-row hit feed showed in the Player frame. In Editor Play the node
inspector's "Combat state" block showed 4/128 projectiles, 4/4 visuals, 2 steps, 4/1024 effects and the Sentry's
effects, and the selected EffectDefine node showed the named "Tick: Damage" choice; the editor smoke
(`ZAMIN_EDITOR_OPEN_ALL_PANELS`, the graph panel and an AbilityDefine node open) 204/204 steps, 0 errors. xdotool is
not installed (installing it was not permitted), so the real ImGui add/save/Undo test of the combo was left for item
4; since the renderer did not change, the render reference, the workshops' Editor phases and the Release bench were
not run.
[Save v4](GameplaySave.md#combat-state-save-v4), [ability links](GameplayCombat.md#ability-links).

Evidence (item 4, 2026-11-21): 1309/1309 unit (2 new: `CombatWorkshopGraphsValidateAndRoundtrip`,
`CombatWorkshopOracleHeadless`), 12/12 headless CTest (with the new `BenchCombatSmoke`), `ctest -L docs` 2/2, the build
without warnings. The compiler-free `Projects/CombatWorkshop` (`Tools/samples/gen_combat_workshop.py`; regeneration is
byte-for-byte identical, `git diff --exit-code` clean): 2D (Box2D, top-down) and 3D (Jolt, character controller)
scenes, 6 effects, the Nova area (LoS by the `Wall` layer), the Bolt/Lance/Venom projectiles with pooled visual
prefabs, 5 player abilities and the Sentry's Spit projectile ability, a quest, 6 save bindings, the effect/hit-marker
HUD and the hit feed; keyboard and gamepad. The oracle is computed from the rules and came out exactly the same in 2D
and 3D: phase 0 (2.45 s) — Sentry 45 = 120 − 30 − 3 − 30 − 6 − 6 (BURN x2), the player 98 HP / 24 shield (Venom into
the shield, Poison through the shield for 1 tick), the targets 13/13/35 (Lance pierce ×3 + Nova only on the two within
3.5 m), the target behind the wall 30 (LoS), Frost on the Nova targets, Attack 10 (Haste), energy 25; phase 1 (a fresh
process) — the saved Bolt in flight hit, a Burn tick killed the Sentry (once), the player 94 (the 3 remaining Poison
ticks), energy 15, Bolt 3, XP 150, Coin 5; phase 2 — a Bolt at the dead Sentry and a Nova without energy changed
nothing, no repeated reward, 0 effects. The phase 0 save: v4, 1 Bolt in flight, Poison/Haste on the player, Burn on
the Sentry (90 HP); the restore log `Combat state restored: 8 effects, 1 projectiles`.
`Tests/combat_workflow_test.py --exports`: 2D/3D × (3 Player phases with a keyboard/gamepad tape + 3 Editor Play
phases) + 2 × export write/resume + a real relocated no-replay = 14/14 workflows, 2 min 7 s; in each the markers, the
absence of GL/graph errors, the PNG and the save JSON were checked (`build/GameplayCombat/`).
Mutation: in the generator, turning off the Nova LoS, letting the shield absorb Poison, making the Sentry fire in the
restored phase, removing the Lance pierce and moving F5 to after the Bolt hit — each failed the headless oracle (5/5,
4–14 checks). Release `ZaminBench --combat` (600 frames, 5 runs): average `TickCombat` 2D 0.069–0.106 ms (p95
0.092–0.150), 3D 0.079–0.101 ms (p95 0.218–0.270), 1024 effect instances 0.047–0.060 ms — the targets (0.5 / 1.0 /
1.0 ms) are met; allocations 9.26 / 12.15 / 21.05 per frame, all of them, by `ZAMIN_ALLOC_TRACE`, from the physics
query vectors and the `RuleWorld` event `std::deque`, the combat code itself does not allocate; baseline
`Tests/Baselines/combat-release.json`, `bench.sh --only combat --compare` 0 slowdowns. Since the design's 512 × 16
effect scenario exceeds the scene's limit of 1024 instances, 64 × 16 = 1024 was measured. Pure
ProjectileModel/EffectModel/RulesModel/LoadoutModel/SaveModel/QuestModel/UIModel ASan/UBSan/LSan: 85/85. An owner probe
of 8 instrumented TUs (CombatWorld, CombatGraph, ProjectileModel, EffectModel, GameplayRules, RulesModel, LoadoutWorld
and LoadoutModel): 42/42 (`CombatWorkshop*`, `CombatWorld*`, `CombatEffect*`), with LeakSanitizer; a sanitized
`CombatBench` with the same TUs (an instrumented `TickCombat` called directly, 128 projectiles, 2D/3D and 1024 effects,
3 × 300 frames) without errors. Since libZamin is bound with `-Bsymbolic-functions`, the workshop graph path inside
libZamin is not instrumented; Scene/serializer/physics/VM and the rest of the shared engine are a normal build — this
is not a sanitizer run of the whole engine. Clang 18 syntax-only -Werror: 3 TUs (`CombatWorkshopTests`, `CombatBench`,
`ParticleBench`); the format check and `py_compile` are clean. Regression: LoadoutWorkshop 14/14 workflows (the save
still v3), SaveWorkshop 6/6 (v1).
Editor: the workflow's 6 Editor Play phases passed; with the QA hooks the node inspector's "Combat state" in Editor Play
showed 1/128 projectiles, 8/1024 effects, Burn on the Sentry and ProjectileDefine's "Kind: Damage" choice.
`Tests/combat_editor_test.py` was written, but xdotool is not installed (installing it was not permitted), so the real
ImGui add/save/Undo test was not run; neither was the render reference, since the renderer did not change. The
headless oracle runs without the projectiles' visual prefabs (a prefab instance builds a font atlas without a GL
context); the visual pool works in the Player/Editor workflows. The Sentry's Spit call is not in its own graph but in
the player's graph (`Target=850`) — that is where the QA phase is. [Playable slice](GameplayCombat.md#playable-slice-combatworkshop),
[profiling](GameplayCombat.md#profiling).

Evidence (review fixes, 2026-11-22): 1315/1315 unit (6 new: `CombatImpactDetonationLineOfSight*`,
`CombatPureNodesNeverCreateTheCombatWorld`, `GameSaveModelV4TickPhase*`, `CombatSaveOwnerlessProjectile*`,
`CombatAbilityCastKeepsEnergy*`, `CombatHUDZeroTicks*`; `CombatSaveUnresolved*` now expects the former owner's UUID),
12/12 headless CTest, `ctest -L docs` 2/2, the build without warnings. Fixes: the area of a projectile that hit a wall
detonates 2 cm in front of the surface (the LoS rays do not start inside the wall that was hit); an ownerless
projectile does not touch its former owner's UUID even after a save and load (an optional `FormerOwner` in v4, written
only when set: without it the document's bytes do not change, old v4 files are read) and its area always admits by the
team snapshot; a tick that changed nothing (a heal at full health) is not written to the hit ring, and the hit marker
takes the newest direct hit from the whole ring; the pure `ProjectileState`/`EffectState`/
`CombatModifiers` nodes do not create a CombatWorld; a Cast whose effect lowered MaxEnergy clamps the energy to the
capacity (a save in that frame now restores); the save codec accepts the tick phase up to 60 + 1e-9, as the model does.
Mutation: with the six fixes (backoff, team admission, former owner, zero tick, energy, tick epsilon) reverted, 6 tests
(5 new + the changed `CombatSaveUnresolved*`, 17 checks) failed; the pure node and hit marker fixes were not reverted
for a check. xdotool (1:3.20160805.1-5build1)
was installed and `Tests/combat_editor_test.py` passed in real ImGui (3 min 17 s): three nodes add/save/Undo
and, through the named choice combo, ProjectileDefine `Kind` → "Heal" and EffectDefine `Tick` → "Damage",
each with save, Undo, Redo and Undo again — the saved graph was checked at every step; the test was added to CI
(together with the combat workflow). The CombatWorkshop framing: the 2D orthographic size 10 → 12, the centre y 1 → 2;
the 3D camera z 12 → 10.5 — in Editor Play the Sentry came out from under the viewport toolbar (2D/3D screenshots).
`Tests/combat_workflow_test.py --exports` 14/14 (1 min 58 s); the 3D free play (R, Space, L, N, M×2) feed has no
"+0 Regen" row and does have "+2 Regen". Regression (Player): LoadoutWorkshop 6/6, SaveWorkshop 6/6. That the editor
toolbar covers the HUD title is common to every workshop (LoadoutWorkshop too) — separate editor work.

### Next package — two finished 2D/3D vertical slices and stress gates

- [x] 2D vertical slice: a menu, at least two levels, motor/combat/AI/quest/loadout/HUD in one game flow, an end screen.
- [x] 3D vertical slice: character controller, camera, Animator locomotion, combat/AI/quest and save in one game flow.
- [x] Animation/physics stress and UI input/scale (keyboard/gamepad, various resolutions) gates.
- [x] Fresh-process resume, relocated export, profiling and regression gates for both slices.

Evidence (2D slice, 2026-11-22): *Lantern Keeper* (`Projects/LanternKeeper`, compiler-free, `ScriptModulePath ""`) —
`Menu`, `Orchards` (46×26), `Crypt` (44×30), victory and defeat screens, the boss Soya Bek;
[VerticalSlices.md](VerticalSlices.md). Engine extensions: a transfer document (`Zamin.GameplayTransfer` v1,
`SaveWorld::CaptureTransfer/RestoreTransfer`, `Gameplay.TransferWrite/Load/Inspect`), the LoadScene fade, App/Audio/
Localization/Display/UIValue nodes, `Gameplay.Steer` and vector math, `FormatNumber`/`FormatTime`, HUD
kinds 7–9 (energy, ability, experience level), AI `Strike=false` (a delegated attack), Animator
`DriveSpriteClips`; the generator kits `Tools/samples/graphkit.py`, `slicekit.py`, `gen_lantern_keeper.py`.
1332/1332 unit (17 new: 4 transfer, 5 slice node/HUD, 1 AI, 3 sprite clip, 4 `LanternKeeper*`; the full run 86 s),
12/12 headless CTest, `ctest -L docs` 2/2, the build without warnings. Headless: the oracle menu → Orchards → transfer →
Crypt → victory in 5022 frames (850 XP, level 4, 5/5 quests, 19 coins = 8 + 6 + the quest reward 5, Sword/Charm/Armour
equipped, Nova/Mend unlocked, the boss dead, 0 projectiles in flight); the defeat plan 5263 frames (3 deaths → defeat →
reloading the save → level 1 again with 200 XP/13 coins, the reward is not repeated); determinism 2252/2252 frames,
23/23 trace lines. `Tests/slice_workflow_test.py` (a new CI step) 7/7 phases, 840 s: P0 a keyboard tape 24 s
(music 0.55 → 0.85, `SLICE_SETTINGS`, a new game, movement/Bolt/strike/inventory/pause), P1 the demo 231 s (level 1
75.0 s, 13 coins, 200 XP; `Carry.json` v1 XP 200, `Run2.json` v4 Crypt), P2 in a fresh process Continue → the boss →
victory 189 s (`time=166.3 xp=850 level=4 coins=19 deaths=0 quests=5/5`), P3 6 s (no Continue, "Best time
02:46"), P4 defeat → reload 300 s (`coins=13 xp=200`, the pilot does not get stuck after the reload), E0
Editor Play Orchards 48 s, E1 Editor Play Crypt next to an old `Carry.json` 41 s (fixed in this step: a level opened
directly does not read an old transfer). The project is 192 files, 5.2 MB; graphs (nodes/links): Hero 560/678,
Game 620/732, Enemy 174/205, Menu 147/160 (limit 2500/5000); running the generator twice gives identical files.
Differences from the design: the levels are smaller (instead of 56×32/48×36), 3 herbs instead of 5, fewer enemies (L1 4
bandits, 2 archers and the chief; L2 4 skeletons, 2 archers, the boss and 2 spirits), no lever/grate/spike traps; one
`Enemy` graph with archetype overrides, the level director in the `Game` graph; settings only in the menu (no in-game
settings and no UI scale). The gamepad tape, UI scale, relocated export and the `samples_assets.py` graph mode were
left for items 3–4.

Evidence (3D slice, 2026-11-22): *Beacon Pass* (`Projects/BeaconPass`, compiler-free) — `Menu` (an evening view of the
pass, a slowly swaying camera) and `Pass` (120×120 m of terrain, a camp, a caravanserai, a stable, the tower plateau,
the beacon; 374 objects); [VerticalSlices.md](VerticalSlices.md), a README (uz), a launcher picture. A character
controller + `Gameplay.Motor3D` (run 5.4, walk 1.8 m/s, jump), an orbit camera (sphere cast), skinned humans from
`gltfkit.py` (21 joints; the hero, the guards, the Black Warden) and the Animator: a Locomotion BlendTree1D (0 / 1.8 /
5.4 — clip length = stride / speed), Air/Land/Dead, the `UpperBody` (Override) and `Hit` (Additive) layers, the
`Hit`/`Release`/`Footstep` events; the strike/fire bolt/Slam on an animation event, delegated guard strikes, archers,
the boss's shockwave, 4 quests (800 XP, level 4), Sabre/Shield with bone attachments, v4 saves at the shrines.
Engine: the `Animator.PlayLayer` `Restart` pin / `PlayLayer(..., restart)` (off by default, old graphs do not change).
1339/1339 unit (7 new: 6 `BeaconPass*` + `AnimatorLayerRestartReplaysTheClipAndItsEvents`; the full run 251 s,
`BeaconPass*` 133 s), 12/12 headless CTest, `ctest -L docs` 2/2, the build without warnings. Headless: the oracle menu →
Pass → victory in 2275 frames (800 XP, level 4, 4/4 quests, 3 stones spent, Sabre+Shield equipped, Slam unlocked,
the Black Warden dead, 0 projectiles in flight, `Run1.json` v4); locomotion (1264 frames): Walk/Run weights 1.00/1.00,
UpperBody 1.00 on a strike, Air → Land, Hurt; step spacing walk 1.007 m (half stride 1.000), run 1.950 m (1.950);
the damped Speed error 0.006 m/s (374 samples, limit 0.05); the feet never sink below the terrain (min 0.000 m, 1264
samples), the camera inside a wall 0/1264; the defeat plan 4272 frames (3 deaths, the Dead state → defeat → reload →
`Pass` once, 100 XP, 0 deaths); save/resume: the courtyard shrine save (frame 1294, the hero at (2.548, 3.001,
1.600), 100 XP, 2 stones, 3 enemies, 5 used objects) restores in a fresh process to within <1 mm (the bag, the quests
and the loadout too), the continuation has 5/5 events in the same order, 300/300 XP, 3/3 stones, the same spot at the
mountain shrine (−5.898, −26.402); a quick save with a Bolt in flight during the Black Warden fight (age 0.133 s)
restores exactly, and 6/6 frames follow the same path (the largest difference 0.00000 m), it hits on frame 7, the
boss's health 260.8/260.8; determinism 2244/2244 frames, 26/26 trace
lines. `slice_workflow_test.py --game BeaconPass` (a new CI step, 640×360 Low) 6/6 phases, 742 s: P0 a keyboard
tape 40 s (music 0.55 → 0.85, a new game, autosave, run/jump/strike/Bolt/bag, pause → `Menu: Inventory`),
P1 the demo 112 s (up to the courtyard shrine: v4, 100 XP, 2 stones, health 120, the combat section present), P2 in a
fresh process Continue → victory 88 s (`time=73.6 xp=800 level=4 stones=0 deaths=0 quests=4/4`), P3 7 s (no
Continue), P4 339 s (3 deaths → defeat → *From the last save* → `Pass` once), E0 Editor Play 155 s; the Lantern Keeper
phases pass in the generalized harness too (P0 33 s, P3 11 s, E0 66 s). The project is 192 files, 6.3 MB (Assets
5.99 MiB; the largest scene 547 KB, model 372 KB, texture 140 KB, sound 516 KB / 12 s); graphs (nodes/links): Hero
655/787, Game 487/580, Enemy 180/211, Menu 155/168, Camera3D 38/46; running the generator twice gives identical files.
Fixed in this step: a rest point at the shrine (the pilot stops, then uses it), the hero reads rule events in
`LateUpdate` — the save is in the same phase as the restore, a restored run starts from the saved state; the heading
(yaw/facing) is restored on resume; the menu camera was moved from inside the pines to above the valley; the `[Look]`
hint became `[LookX|Mouse]`. Differences from the design: the Black Warden has 280 HP (not 420), 4 guards (not 5),
the terrain is 120 m/81 (not 128/129), no `meshkit.py` OBJ kit (the caravanserai is made of textured primitives +
gltfkit props), the `Pass` quest is 2 gate guards + opening the gate (not a Manual receipt), settings only in the menu;
since the restore stops the bodies and nav agents (the save keeps no velocity/path), the continuation gives the same
events/result, while the frames may differ. The gamepad tape, UI scale, relocated export and profiling are in items
3–4.

Evidence (stress and UI gates, 2026-11-22): [VerticalSlices.md](VerticalSlices.md#gates),
[Benchmarks.md](Benchmarks.md). Stress (headless `ZaminBench`, Release, 4 vCPU Xeon 2.1 GHz, 5 runs, ranges — the
smallest/largest): P1 `--physics --2d` (a 60×40 m tilemap, 2 048 bodies, 64 `StepMotor` actors, 128 projectiles)
average physics step 1.56–2.15 ms, p95 2.3–3.1 (budget 4/6); P2 `--physics --3d` (a pit, a ramp, 0.25 m stairs, a
35° slope, 65×65 terrain, 512 bodies, 64 Character Controller 3D, 128 sphere casts) step + CC 1.51–1.80 ms, p95
2.00–2.17 (8/12), the casts 0.23–0.28 ms; A1 `--animation` 256 humans (`Hero.glb`, 21 bones, the Pass controller,
half of them off screen) on 3 workers 0.57–0.82 ms, p95 0.66–1.10 (1.5/2.0), on one thread 0.99–1.73 ms (4.0),
allocations 0.0 / frame; A2 1 024 sprite animators (`DriveSpriteClips`, 2D blend directions) Animator update
0.30–0.48 ms (0.6), allocations 0.0; P3 `--slice-stress --2d` (Orchards: 32 brains, 200 crates, 64 projectiles, 256
sprite animators, 48 Light 2D) game code 2.80–3.42 ms, p95 3.00–4.71 (6/10); A3 `--slice-stress --3d` (Pass: 24
animated guards, 27 brains in all, 64 projectiles, 200 Jolt bodies) 3.89–5.45 ms, p95 5.41–6.37 (8/12). The
invariants held every time: tunnelling/NaN 0, the largest 2D speed 6.88 m/s, the CC's feet at most 0.0002 m below the
terrain, ≥ 90.7 % of the commanded path covered, the two simulation hashes equal; the blend weight error ≤ 3e-8, the
0/3-worker pose hashes equal, the events the same per animator at 20/30/60/144 Hz, sprite frame/phase breaks 0 (over
14 223 clip switches), graph errors 0. The baselines are in `Tests/Baselines/{physics,animation,slicestress}-release.json`,
`bench.sh --compare` works (an animation result that looked slow passed on re-measuring);
`BenchPhysicsSmoke`/`BenchAnimationSmoke`/`BenchSliceStressSmoke` are in `ctest -L bench`. Fixed in this step: when
the direction changed, a sprite clip fired the event of the current frame a second time
(`SpriteAnimationComponent::DrivenState`, `AnimatorSpriteClipsDirectionSwitchesFireEachStepOnce`); the Animator update
on one thread now allocates nothing (before, 223 allocations / frame for 24 humans and 870 for 64 sprites; the latter was
the pose signature growing, found with `ZAMIN_ALLOC_TRACE`). UI: `Scene::AuditUI` + `CheckUIAudit` (`Margin`,
`Offscreen`, `Overlap`, `TextOverflow`, `MinFont` 12/14 px, `Focus`, `Unreachable`, `SideBand`), `ZAMIN_UI_AUDIT` in
the Player, `Scene/SceneManager::SetUIScaleMultiplier`, Blueprint `Display.SetUIScale`/`Display.UIScale`;
*Interface size* 80–120 % in both slices' settings (`SLICE_SETTINGS ui=`, saved in `Settings.UIScale`, applied at the
start). Headless `SliceMenuLayout*`: 15 + 8 screens × 6 sizes (800×600 … 2560×1080) × 3 UI scales = 270 + 144
audits, 13 932 + 5 472 elements, 0 errors, the smallest text 9.6 px (at 80 %). `Tests/slice_ui_test.py` (a new CI
step) passed in both games: U1 a keyboard tour 60 audits, 6 screens, 0 errors, the smallest text 12.03 px (65/70 s);
U2 a fresh process opens at 120 %, a gamepad tour returns it to 80 % (60 audits, 59/64 s), the mouse clicks
*Settings* and *Back* where the audit said; U3 a seeded fuzz of 2 000 frames × keyboard/gamepad: 534/504 (Lantern
Keeper) and 546/450 (Beacon Pass) audits, at most 2 panels, 0 invariant breaks, 0 errors (145–165 s); UW 2560×1080
menu 6 audits. Mutations (Player): with the music slider's links broken U1 failed (the tour does not reach *Back*, the
audit reports `Unreachable` for three widgets), with the version line at 18 units instead of 25 U1 failed with 12
`MinFont`; the headless `SliceMenuLayoutCatchesABrokenLinkAndTinyText` checks the same. Layout fixes: texts ≥ 25
units, the controls row wraps at 78 % of the screen width, the 2D menu map covers the 21:9 edge (before, a black band
on the left). 1345/1345 unit (6 new: 1 animator, 5 UI audit/layout; the full run 282 s), 14/14 headless CTest
(`-LE gui`, ZaminTests separately), `ctest -L docs` 2/2, the build without warnings; `slice_workflow_test.py` P0+E0 in
both games (Lantern Keeper 38 + 70 s, Beacon Pass 42 + 176 s); the generators twice — identical files. Differences
from the design: the in-game stress (A3, P3) runs headless with `--slice-stress` on copies of the real levels instead
of the Player's `--benchmark` and separate `Stress` scenes (the assets within the 6 MiB budget, llvmpipe drawing would
hide the game code time; no `slice_bench.py --stress`); the canvases stayed at *Match 0.5*, not *Expand* — the
layouts were fixed, so instead of an "Expand turned off" mutation there are the small font and broken link mutations;
`MinFont` shrinks in proportion when the UI size is below 100 %; the 15.2 allocations / frame on 3 workers are
`JobSystem::ParallelFor` itself; the allocations in physics (2D 128.5, 3D 82.6 / frame) are Box2D/Jolt query results
— no budget set, recorded.

Evidence (resume, export, profiling and regression gates, 2026-11-22): **Resuming in a fresh process** — the
headless `*ResumeMatrixHeadless` keeps, from one ordinary game, the files a process killed at every step would leave,
and starts a fresh process on every copy (`SaveData::Reload`, `GameState::Load`, the demo menu's *Continue*):
Lantern Keeper 8 cases (the transfer written at the level 1 exit, `Run.Level` still 1 → from the shrine save, the same
transfer 200 XP; after GameState / after the autosave → level 2 through the transfer, 200 XP, 13 coins; a broken
checksum → the backup, *Restored from the backup save*, `recovered=1`; the main and backup torn → `SLICE_IO_FAILED load`,
on screen *The save could not be read - the level starts again*, with the transfer torn too a clean level 2), Beacon
Pass 5 cases (between the new game and the autosave, between the shrine save and GameState — 100 XP, two stones, the
state < 1 mm; broken → the camp autosave; torn → a clean start); no reward is ever given twice, the pilot step
(`Run.Step`) 0–2. Mutation: removing *Continue*'s `Run.Save == Run.Level` condition fails the matrix 3 times.
**Player/Editor** (`Tests/slice_workflow_test.py --exports`, both games, the CI steps with `--exports`): Lantern Keeper
P0 54 s, P1 417, C 51 (broken → the backup 200 XP; torn → the transfer 200 XP + a banner), P2 292, P3 8, P4 461, P6 50
(the gamepad tape: the D-pad music 0.85, A a new game, the stick walked the hero 7.17 m, RB/X, Back/B the bag, Start →
*Save and quit to menu* `SLICE_SAVED lvl=1 at=quit` v4, the menu offers *Continue*), E0 80, E1 69, E2 54
(in Editor Play the menu → a new game → the level), D 25 (the Player 960x540 / 640x360 trace has 23/23 events the same
as headless), X 545 s (the export 215 files 54.4 MiB; the attract run was stopped with `ZAMIN_MAX_FRAMES` after the
level 2 autosave (200 XP), the folder was really `mv`ed, a fresh process from the new place to victory: 166.3 s,
850 XP, level 4, 19 coins, 0 deaths, 5/5 quests; in the third process no *Continue*, the fourth a new game 0 XP, the
record 166.3 kept); Beacon Pass P0 66, P1 178, C 58 (broken → the camp autosave; torn → a clean start + a banner), P2
110, P3 8, P4 398, P6 49 (the stick 12.46 m, the quit save v4), E0 176, E2 155, D 41 (the first 12 of the 26 headless
events the same), X 337 s (209 files 55.5 MiB; stopped after the courtyard shrine save (100 XP), victory from the moved
place: 73.6 s, 800 XP, level 4, 4/4 quests, no *Continue*, a new game 0 XP, the record kept). **Editor**:
`Tests/slice_editor_test.py` (a new CI step) added/saved/undid `Gameplay.TransferWrite`, `TransferLoad`, `Steer` in the
real Blueprint window; LoadScene `FadeOut` was set to 0.6, the UIBindHUD Kind combo was turned to 8 *Ability*, the
AIConfigure `Strike` checkbox was cleared — each saved, Undo/Redo; `ZAMIN_EDITOR_GRAPH_QA` (a new hook) opened the 9
graphs of both slices in the window (Lantern Keeper Hero 560/678 nodes/links, Game 625/737; Beacon Pass Hero 655/787,
Game 490/583 …), 0 diagnostics in all of them, no errors in the log; `Tests/editor_smoke.sh
Projects/LanternKeeper Projects/BeaconPass` (a CI step) 243/243 and 268/268 steps, 0 errors. **Profiling**
(`Tests/slice_bench.py`, Release, llvmpipe, the best of 3 runs, `Tests/Baselines/slices-release.json`): a new
`GameCode` in the Player benchmark report (the update layer without drawing); at 284x160 Low the game code of Lantern
Keeper Menu / Orchards / Crypt 0.32 / 2.02 / 2.03 ms, Beacon Pass Menu / Pass 0.53 / 3.50 ms, p95 at most 4.40 ms (the
gate ≤ 3 / ≤ 4 ms, p95 ≤ 6); the frame in 2D 3.48–5.67 ms (≤ 16.6), in 3D 22.2–28.2 ms (≤ 33.3); in a warm session the
slowest frame ≤ 51 ms (in the first session llvmpipe's shader JIT: Crypt 1.9 s, Pass 345 ms — in a separate column);
`--compare` (`bench_compare.py --tolerance 15`): with 2 runs 3/40 numbers at 720p turned out to be noise (0 slowdowns
in 16 numbers on re-measuring) — hence 3 runs by default and the 720p p95 marked `Noisy`. **Soak** (`--soak 18000`,
the attract loop, 10 game minutes): Lantern Keeper 3 victories, 57 objects at every return to the menu, RAM 302.9 MiB,
engine 99.2 MiB (growth 0.0 MiB); Beacon Pass 7 victories, 278 objects, RAM 269.4 → 278.8 MiB and flat after that
(growth 9.5 ≤ 32 MiB), engine 39.0 MiB; no errors in the log. **Bugs found and fixed**: (1) after an unreadable save,
level 2 loaded the transfer and failed to load the autosave in the same frame — 3 file operations, the scene budget 2 —
the autosave failed (`SLICE_IO_FAILED autosave`); now one frame later; (2) the save at the start of a level did not
clear the demo pilot's `Run.Step` — an attract run continued in the relocated export got stuck on level 2 starting
from level 1's step (`SLICE_BOT_STUCK step=31`); now `Run.Step=0` in both generators; (3) an unreadable save was
silent — now a banner; (4) content: a warning about an unknown `rot` component on Pass's *Sun*, an invalid
`Gamepad:LeftTrigger` on Lantern Keeper's Sprint (→ `GamepadAxis:LeftTrigger+`), raw `[Slam]` / `[Nova]` in the banners
(now the key name). The gallery: two slices in `docs/Samples.md` (`sample-lanternkeeper.jpg`, `sample-beaconpass.jpg`),
the launcher thumbnails updated. 1347/1347 unit (2 new: the resume matrix of both slices; the full run 320 s), 14/14
headless CTest (`-LE gui`, ZaminTests separately), `ctest -L docs` 2/2, the build without warnings; the generators
twice — identical files. Regression (Player/Editor, `--exports`): the Gameplay, Rules, AI, UI, Save, Quest, Loadout,
Combat, Animator and Blueprint workflows 10/10 (86 phases, each 95–202 s); after the content fixes the slice P0, P1, C,
P6, D passed again in both games. Differences from the design: no separate keyboard phase (P5) — P0 plays with the
keyboard; E1 only in 2D; in X the "kill" is done with `ZAMIN_MAX_FRAMES` between two saves (cuts in the middle of a
write are in the headless matrix); 3D determinism by the 12-event common prefix of the Player and headless traces;
`slice_bench.py` is not in CI (it needs Release and a quiet machine), the Player does not count allocations (RSS and
engine memory are recorded), the 3D frame limit is 33.3 ms (llvmpipe), the first-session hitch is recorded separately
outside the gate; the soak does not measure the font atlases / VM size separately.

Evidence (fixes for the reviewers' findings): `FindUnexpandedInputToken` for raw `{action:...}` tokens and the UI audit's `RawHint` rule (a gate in the Play tests of both games); graph texts, banners, HUD/statistics, level names and item/quest/combat feed names go through localization keys (`item.*`, `quest.*`, `combat.*`; Pass now shows "-2 Burn"), the uz/en keys are checked in the `samples_assets.py` graph mode; the settings are saved on every close (Back and Back/Escape), the language and fullscreen are restored in a fresh process with `Localization.SaveSettings` / `Display.SaveSettings` (`SliceMenuSettingsKeptOnEveryCloseAndLanguagePersists`, which fails with the old menus); healing at the shrine comes before the save; the level card stops the world, counts in real time, does not pass with a dead hero and retries after 1 s on a `TransferWrite` error (`LanternKeeperShrineSaveAndLevelExitCardHoldUp`); the boss resumes in phase 2 (`LanternKeeperBossFightResumesInPhaseTwo`, Enraged in Pass); the camera stick is speed × Delta and stops while the bag is open (`BeaconPassCameraStickTurnsAtARate`: −1.200 rad in 0.5 s at 30 and 60 Hz, 0 behind the bag; the old graph −0.63 / −1.26); `Gameplay.Input` does not read empty action names; the VM keeps every engine exec result. Visuals: in Lantern Keeper a 47-variant blob path autotile, a water autotile, seamless grass, a `Decor` layer; the contrast of the HUD energy/XP bars; in Pass triplanar steep slopes, radial mesh peaks, a death pose, lighter fog (0.0045). The oracles moved to `Tests/oracle.json` (not part of the export); `SLOW_TEST`: `ZaminTests --quick` 1341/1341, 61.7 s; `ZaminSliceTests` (`--slow`) 12/12, 326.6 s; `ctest -LE gui` 17/17, `ctest -L docs` 2/2; assets: Lantern Keeper 5.31 MiB, Beacon Pass 5.96 MiB; `slice_ui_test.py` U1/U2 in both games (60 audits, 0 problems); `slice_workflow_test.py` P0 and P6 passed in both games (41–56 s).

### Finished section — gameplay UI

- [x] Bounded modal stack, focus/pause ownership and input fencing.
- [x] HUD/rules bindings, deterministic inventory pages and live selection.
- [x] Blueprint/native API, lifecycle and real keyboard/gamepad isolation.
- [x] 2D/3D playable UI/save-resume workflow, scale and export screenshots.
- [x] Unit, sanitizer, regression, documentation and main push.

Evidence (2026-11-12): 1160/1160 unit (42 new), 11/11 headless CTest, 12/12 render.
Pure UIModel/RulesModel ASan/UBSan/LSan: 10/10. An owner probe with the UIModel, GameplayUI, GameplayUIGraph and
GameplayRuntime TUs instrumented: 42/42, with LeakSanitizer; Scene,
SceneUI, SceneUIScroll, the serializer, physics, the VM and the rest of the shared engine are a normal build.
This is not a sanitizer run of the whole engine. Clang 18 syntax-only -Werror: 10 TUs.
Real ImGui: UIConfigure/UIBindHUD/UIInventoryConfigure add/save/Undo. A 2D/3D real keyboard/gamepad
Player replay (225 frames), an inventory capture (110), a 1024x768 capture (110), Editor auto-play (225),
a compiler-free export benchmark/screenshot (225): 10 workflows in all. After the save HP/coins are deliberately
lowered; Load must bring back the native 75 HP / 8 coins. A relocation --version smoke check.
After the last item-tint polish 4 more inventory captures/oracles (1280x720 and 1024x768) passed.
The generator is deterministic; reports/screenshots: `build-review/GameplayUI/`. Software Mesa;
the physical GPU budget was not measured. [Scope, timing and input conditions](GameplayUI.md).

### Finished section — gameplay save/load

- [x] Strict bounded versioned snapshot codec with integrity checks and stable entity keys.
- [x] Atomic slots, validated backup recovery, per-scene I/O and spawn budgets.
- [x] Health/bag/interaction/AI/pose restoration and persistent prefab-slot bindings.
- [x] 2D/3D fresh-process resume, real Blueprint authoring, Player/export/screenshots.
- [x] Native, sanitizer, regression, documentation and main push.

Evidence (2026-11-12): 1118/1118 unit (41 new), 11/11 headless CTest, 12/12 render.
Pure SaveModel/RulesModel ASan/UBSan/LSan: 14/14. An owner probe with the SaveModel, SaveWorld, SaveGraph and
GameplayRuntime TUs instrumented: 41/41, with LeakSanitizer; Scene,
the serializer, physics, the VM and the rest of the shared engine are a normal build. This is not a sanitizer run of
the whole engine. Clang 18 syntax-only -Werror: 9 TUs. Real ImGui: SaveBind/SaveWrite/SaveLoad add/save/Undo.
The SaveWorkshop generator is deterministic; the configuration compatibility and root-only slot conditions are in
[GameplaySave.md](GameplaySave.md). Report/screenshots: `build-review/GameplaySave/`.
2D/3D Player write/fresh-process resume/corrupt-primary backup, Editor write/resume and compiler-free
export write/resume/real relocated resume: from 150 frames, 12 workflow checks in all.
The native HP/bag/used-door/AI arrivals/player pose/prefab-slot oracles passed; relocation is not limited to
--version. Software Mesa; the physical GPU time budget was not measured.

### Finished section — AI gameplay

- [x] Pure patrol/chase/investigate/attack/return FSM, memory/leash and attack hysteresis.
- [x] Shared hostile perception/explicit strike, LOS/team filtering and bounded scene budgets.
- [x] Native/Blueprint AI configuration/state/events, navigation ownership, recovery, cancellation and lifecycle.
- [x] 2D/3D NPC prefab/course, physical movement/combat, Editor/export and screenshot acceptance.
- [x] Unit, sanitizer, regression, documentation and main push.

Evidence (2026-11-12): 1077/1077 unit (38 new), 11/11 headless CTest, 12/12 render.
Pure BrainModel ASan/UBSan/LSan: 14/14. An owner probe with the BrainModel, GameplayAI, GameplayAIGraph, GameplayRules,
GameplayRuleGraph and GameplayRuntime TUs instrumented: 24/24, with LeakSanitizer.
Scene, the serializer, physics, the VM and the rest of the shared engine are a normal build in this probe;
this is not a sanitizer run of the whole engine. Clang 18 syntax-only -Werror: 11 TUs.
2D/3D Player input replay and Editor auto-play: from 420 frames; a compiler-free export benchmark
from 420 / a screenshot from 450 frames, 6 workflows in all. The patrol/chase/attack/investigate/return/dead/spawn
and canonical HP/prefab-nav oracles passed. Relocation was checked with the launcher's --version.
Real ImGui authoring: AIConfigure/AIState/Sense add/save/Undo. The AIWorkshop generator is deterministic.
Reports and screenshots: `build-review/AIGameplay/`. Software Mesa was used;
the physical GPU budget was not measured. AI is runtime-only, the configuration is stored in graph assets.
[AI conditions and limits](GameplayAI.md).

### Finished section — health, combat, inventory and interaction

- [x] Scene-owned shared state, bounded transactional health/shield/damage/heal and cooldown clocks.
- [x] Death, checkpoint-aware respawn, motor gating and bounded rule event polling.
- [x] Bounded inventory, transactional key/reward/heal interactions, deterministic range/facing/LOS queries.
- [x] Blueprint/native APIs, melee attack selection and real 2D/3D combat/collectible courses.
- [x] Unit/lifecycle/physics, sanitizer, UI, Player/export, screenshots, docs and main push.

Evidence (2026-11-11): 1039/1039 unit (51 new), 11/11 headless CTest, 12/12 render.
Health/inventory leaf ASan/UBSan/LSan: 20/20. An owner probe with the RulesModel, GameplayRules, GameplayRuleGraph and
GameplayRuntime TUs instrumented: 51/51, with LeakSanitizer; Scene,
the serializer, physics, the VM and the rest of the shared engine are a normal build in this local probe.
Clang 18 syntax-only -Werror: 8 TUs; this is not a sanitizer run of the whole engine.
Real ImGui authoring: Damage/InteractionConfigure/Respawn add/save/Undo. 2D/3D Player input
replay and Editor (240 frames), a compiler-free export benchmark (240) / screenshot (270): 6
workflows in all, death/respawn/key/kill/heal/unlock/coin/finish and the canonical HP/bag/guard oracles.
Relocation was checked through the launcher's --version. The new pause-budget and skipped-dead-frame life
invalidation regressions were checked too. The old SceneLoadTimingBreakdown test's multi-frame assumption, which
depended on worker scheduling, was made deterministic with an activation gate; 10/10 independent runs passed.
Report and pictures: `build-review/GameplayRules/`. Software Mesa; the physical GPU budget was not measured.
The graph configure inputs are saved; the health/inventory state is runtime-only. [Conditions](GameplayRules.md).

Gameplay save/load continued in the package above. A full ECS/VM/physics snapshot,
automatic resurrection of arbitrary objects and async saving of a large world remain separate.

Gameplay UI, quest/progression, equipment/abilities and projectiles/gameplay effects were brought together in the
packages above. The next large package — two finished 2D/3D vertical slices and stress gates; its acceptance
gates are set out above. The full ECS/physics snapshot, renderer and large-world tasks stay in the general
plan.

### Finished section — motor, camera, locomotion and checkpoint

- [x] 2D/3D motor: input, acceleration/deceleration, analog clamp, jump buffer/coyote/cut and lifecycle stop.
- [x] 2D follow/3D orbit camera, damping, bounds, obstruction and restore snap.
- [x] Actual physics velocity → typed Animator binding, reusable Blueprint authoring.
- [x] Bounded checkpoint codec, atomic persistent save/load and physics-aware restore.
- [x] Playable 2D/3D samples, UI/runtime/export, regression/sanitizer/screenshots and main push.

Evidence (2026-11-11): 988/988 unit (46 new), 11/11 headless CTest, 12/12 render.
Motor/camera/codec leaf ASan/UBSan/LSan: 22/22. An owner probe with the three Gameplay TUs, the graph
VM/model/function and the host instrumented: 46/46, with LeakSanitizer. Scene/serializer/physics and the rest of the
shared engine are a normal build in this local probe; this is not a sanitizer run of the whole engine.
Clang 18 syntax-only -Werror: 7 changed TUs. 2D/3D Player input replay (180 frames), fresh-process
resume (60), Editor auto-play (180), a compiler-free export benchmark (180) and a screenshot run (210)
passed the movement/jump/checkpoint/goal/restore oracles. Relocation was checked through the launcher's --version.
Real ImGui authoring: motor/camera/checkpoint node search, save and Undo. CI runs these two workflows
again. Report and pictures: `build-review/GameplayPackage/`; software Mesa, the hardware budget was not measured.

This package completes the movement → physics → animation → camera → checkpoint chain.
The motors work on top of Rigidbody2D/CharacterController3D; the checkpoint stores the actor transform.
A full inventory/ECS snapshot, humanoid retargeting and a cinematic camera are not part of this package;
the checkpoint clearance is a central probe, not a full capsule fit. The conditions are in [Gameplay.md](Gameplay.md).

Reusable health/damage/death/respawn, interaction, combat/collectibles and the Blueprint workflow
continued in the gameplay rules package above.
The P2 content and the P3/P4 world/renderer tasks stay in the general plan above.

### Finished section — Animator gameplay and 2D blend

- [x] Cartesian 2D blend solver, skeletal pose/root motion/event integration and synchronized phase.
- [x] Typed parameters/damping, priority and fade interruption policy, self restart and model-free state clocks.
- [x] Serialized configuration, validation, editor blend field/live telemetry and transition controls.
- [x] Blueprint Animator operations, real 2D/3D gameplay samples and export acceptance.
- [x] Regression, sanitizer, screenshots, documentation and main push.


Evidence (2026-11-11): 942/942 unit (31 new), 11/11 headless CTest, 12/12 render.
CPU solver/controller ASan/UBSan: 20/20. An owner probe with the new blend/controller/Blueprint bridge and three
VisualGraph TUs instrumented: 31/31, with LeakSanitizer; Scene/serializer and the rest of the shared engine
are a normal build in this local probe. Clang 18 syntax-only -Werror: 11 changed TUs.
Real ImGui: sample drag/save/Undo, creating a 2D state/Undo. Player/editor from 120 frames in both scenes:
Action was seen, Locomotion came back, damping/blend weight0 = 0.63212055 and the earlier Blueprint total 12/18.
In both exports the real gameplay log, the PNG and the relocated launcher were checked. Evidence:
`build-review/AnimatorGameplay/`. Mesa software rasterizer; hardware performance was not measured.
Editor smoke: 490/490 steps (224 Showcase3D + 266 Platformer2D), 0 errors. Finished with the main push.

Next section: a 2D/3D gameplay vertical slice with input/controller/camera and binding the real velocity →
Animator parameters, checkpoint/save and play/export acceptance.

### Finished section — Blueprint gameplay acceptance

- [x] Typed function signatures, parameters/results, isolated locals, nested/delayed calls and bounded recursion.
- [x] All seven typed arrays, safe operations and snapshot ForEach; literal/value/event memory limits.
- [x] Named local events, validated dynamic pins/scopes, runtime diagnostics and call stack.
- [x] Editor signature/array authoring, multi-select/copy/paste/duplicate/delete, rename and undo.
- [x] Compiler-free 2D/3D workshop, real UI/runtime/export oracles, sanitizer/regression, docs and main push.

This section completes the gameplay graph's author → validate → run/debug → export chain.
Network RPC, graph inheritance and native code generation belong to the P6/toolchain phases;
they are not counted as hidden "remaining work" for this acceptance.

Check (2026-11-11): 911/911 unit (31 new), Blueprint focused 31/31, headless CTest 11/11.
Clang 18 syntax-only -Werror: 7 changed TUs. The graph VM/model/function and 30 CPU tests
were checked with ASan/UBSan/LeakSanitizer; the leaf probe uses a small stub only for writing files,
the whole engine and the atomic write to disk are not instrumented in this sanitizer probe. The real I/O and scene
lifecycle were checked in the normal unit suite. Evidence: `build-review/BlueprintCompletion/`.
The real ImGui authoring test checks typed wires/rejection, Undo/Redo, copy/paste/duplicate/delete and group edits.
The 2D/3D workflow runs the parameter/local/Delay/ForEach/custom event chain without a compiler or a native game
module, with the result 12/18. CI runs this workflow, the export and the editor authoring checks again.

The animation state machine/blend and gameplay integration continued in the Animator package above.

### Finished large package — dynamic obstacles and bounded recovery

- [x] Same-frame world atlas publication after carving; synthetic mosaic excluded from carve feedback.
- [x] Recovery watchdog: real progress, pending/portal pause, bounded retry/cooldown/exhaustion and reset.
- [x] Serialize/copy/reset, inspector and Blueprint recovery diagnostics/manual retry.
- [x] Live barrier 2D/3D demos, portal/async/physics regression, sanitizer, screenshots and native exports.
- [x] CI failure audit, verified evidence, upstream check and main push.

Local evidence (2026-11-11): 880/880 unit (19 new), 138/138 focused, 11/11 headless CTest,
12/12 render, 490/490 editor smoke, OpenGL 4.1 residency GPU. Clang 18 syntax-only -Werror: 14 TUs.
NavRecovery leaf ASan/UBSan: 10/10. An owner probe with the recovery, source carving, world movement and world service
TUs and the focused tests instrumented: 138/138; the rest of the shared engine is a normal build,
this is not a sanitizer run of the whole engine. 2D/3D Player from 420 frames; the editor from 160 frames, an
8-node/9-link/4-variable graph and the live Exhausted/Phase/Retries above it. Both native exports passed 420 frames,
the oracle, the PNG and relocation. The earlier NavAgent/portal Player regression passed at 420 and the crowd one at
450 frames, in both sizes. Evidence: `build-review/RecoveryNavigationPackage/`.

The 763a7b0 remote coverage run stopped in the dependency stage with an Azure and official Ubuntu mirror DNS error;
this is not a C++ test failure. The installer does not reinstall cached packages, reports clearly and quickly when both
DNS fail, and every update/install command is bounded by a timeout.
The mock regression does not change the real apt/sudo or the host's mirror files. The new remote CI is watched
separately after the push; the code cannot guarantee that external DNS works.

Limits: opt-in recovery; when a physics-only obstacle is removed an explicit retry, when a NavObstacle/the topology
changes an automatic reset. The atlas build is on the owner, there is no hard frame deadline/hardware profiling.
Next package: animation locomotion and a gameplay vertical slice; a precise acceptance with the profiler for a large
scene, measuring the streaming/query times.

### Finished large package — world smoothing and crowd steering

- [x] Sparse atlas segment clearance: holes, corners, floor/climb, bounded work.
- [x] Worker-side bounded smoothing; weighted costs and explicit portal endpoints preserved.
- [x] Deterministic snapshot crowd steering, spatial broadphase, nearest-neighbour and candidate caps.
- [x] World NavAgent integration, conservative movement clipping, serialized settings, inspector and graph telemetry.
- [x] 2D/3D crowd demos, regression/sanitizer/runtime/export screenshots, upstream check and main push.

Local evidence (2026-11-11): 861/861 unit (22 new), 119/119 focused, 10/10 headless CTest,
12/12 render and 490/490 editor smoke steps; OpenGL 4.1 residency GPU. Clang 18 syntax-only -Werror:
14 TUs. Crowd/atlas/segment/grid leaf ASan/UBSan: 17/17, not a sanitizer run of the whole engine.
2D/3D crowd Player from 450 frames and the editor from 130 frames: 4 agents, two destinations, Stop/reverse, graph
telemetry, separation; a 5-node/6-link/3-variable graph. Both native exports passed 450 frames, the oracle, the PNG and
relocation. The earlier NavAgent/portal/unload/return Player regression passed in both sizes from 420 frames.
Evidence: `build-review/CrowdNavigationPackage/`. Checked with software Mesa; hardware performance was not measured.

The 51eead3 remote CI stopped in the dependency stage because of an Azure Ubuntu mirror DNS error, and the C++ check
did not run in that run. A shared bounded apt retry/timeout and an official archive fallback were added;
the workflow YAML/bash syntax was checked. The state of the new remote CI is watched after the push.

Limits: a conservative swept rectangle and bounded string pulling, not a world spline. Crowd velocity
sampling, not ORCA nor a collision guarantee; physics has the final say. A dense overflow shows as a stop/diagnostic.
Next package: dynamic obstacle/repath recovery, multi-agent profiling and animation locomotion.

### Finished large package — bounded async world navigation

- [x] CPU-only worker queue, immutable snapshot lifetime, FIFO start/publication budgets.
- [x] Active cancellation, replacement, retained snapshot/result byte budgets, bounded shutdown.
- [x] NavAgent async admission, pending stop, stale destination/start/revision rejection, cancel/unload/destruction.
- [x] Policy serialization, editor queue diagnostics, real 2D/3D graph/controller demos.
- [x] Concurrency/regression, sanitizer, screenshots, native export and main push.


Local evidence (2026-11-11): 839/839 unit (19 new), 97/97 targeted tests,
10/10 headless CTest, 12/12 render, 490/490 editor smoke steps, OpenGL 4.1 residency GPU.
Clang 18 syntax-only -Werror: 9 changed TUs. A fully instrumented ASan/UBSan leaf probe of queue/atlas/grid and the
queue tests: 10/10; this is not a sanitizer run of the whole engine.
2D/3D Player from 420 frames and the editor from 120 frames passed with a 13-node/16-link/3-variable graph.
Both native exports passed 420 frames, the async agent oracle, the screenshot and relocation.
The earlier 2c8585c CI is green in every check. Evidence: `build-review/AsyncNavigationPackage/`.

Limits: the scratch allocator/RSS and a hard frame-time deadline are not part of these payload budgets;
the atlas build is on the owner thread. Movement is sequential corner tracking; local avoidance, path smoothing,
jump/elevator animation and a general job scheduler are left for later packages.
Next package: route smoothing with collision preserved and multi-agent local avoidance.

### Finished large package — NavAgent, controller and Visual Graph traversal

- [x] NavAgent opt-in sparse atlas, bounded query, actual-position corner tracking, stale replan.
- [x] Character controller collision, safe exit occupancy, revision/link completion token and cancel.
- [x] Serialize/copy/reset, C++ API, inspector runtime state.
- [x] Visual Graph world destination, traversal state/event, complete/cancel and success outputs.
- [x] A real 2D/3D agent/graph demo, runtime screenshots, regression and native export.
- [x] Test evidence, documentation, an upstream merge check and main push.

7d12f4a remote CI: format, GCC, Clang, ASan, UBSan and coverage fully green.

Local evidence (2026-11-11): 820/820 unit (10 new), 10/10 headless CTest, 12/12 render,
490/490 editor smoke steps, GL 4.1 residency GPU; Clang 18 syntax-only -Werror 12 files.
A targeted ASan/UBSan probe with the agent/VM TUs and tests instrumented: 78/78; the shared engine is a normal
build, this is not a full engine sanitizer run. With fixed capture 30, the 2D/3D Player from 420 frames: three
real traversals/arrivals, disable/unload/return; every editor from 120 frames with a 10-node/14-link graph preview.
Both native exports also passed 420 frames, the agent/graph oracle, the screenshot and relocation.
The new inspector keys in the translation dictionary and a mode mutation regression before a float overflow were fixed.
Evidence: `build-review/AgentTraversalPackage/`. The new remote CI is watched separately after the push.

Scope: opt-in sequential world queries, a per-query expansion cap; a corner route. Character exit
occupancy checked; Rigidbody traversal completion, world local avoidance/smoothing, jump/elevator
animation and async query scheduling are later work. The Transform/controller BaseOffset difference is taken into account.


### Finished large package — sparse navigation and traversal workflow

- [x] An immutable atlas that depends only on the size of the loaded tiles; a bounded and deterministic route query.
- [x] Two/three dimensions: tile borders, floor/climb, cost, carving and partial path rules.
- [x] Directed, costed traversal links that can be disabled; a missing endpoint is safely inactive.
- [x] Route follower: stopping before a link, explicit completion, stop/replan on stale topology.
- [x] World runtime query/API, editor route diagnostics, a real 2D/3D demo and screenshots.
- [x] Regression, build, export, documentation and main push.

The earlier 73a683c remote CI: GCC, Clang, ASan, UBSan, coverage and format fully successful.

Evidence (2026-11-11): 810/810 unit (18 new), 10/10 headless CTest, 12/12 reference render,
490/490 editor smoke steps, the GL 4.1 residency GPU oracle; Clang 18 syntax-only -Werror 7 files.
A separately instrumented ASan/UBSan run for atlas/grid/pathfinder/mosaic/follower: 16/16 tests;
this is not a full engine sanitizer run. 2D/3D Player from 420 frames portal/disable/stale/unload/return,
both editors from 120 frames route preview. The earlier camera tours from 660 frames (6 complete
cross-cell paths in each), the checkpoint from 540 frames edit/delete/adopt/unload/return/restore. Both native exports
passed the navigation oracle from 420 frames, the screenshot and relocation. Evidence: `build-review/NavigationPackage/`.

The snapshot build is synchronous and bounded; incremental baking is still later work. The traversal follower is
kinematic; character collision/animation is the caller's, the old NavAgent uses the dense mosaic. The next navigation
integration: NavAgent/character controller and Visual Graph traversal nodes. A polygon navmesh is separate.


### Finished large package — world authoring and checkpoint workflow

- [x] A safe checkpoint format: bounded cell snapshots, world identity, atomic save, no mutation on a damaged file.
- [x] Runtime: unload/return state, deleted/spawned entities, an ownership API, root local/world coordinates.
- [x] Asynchronous restore: the snapshot JSON parsed on a worker, cancelling an old generation, a staged world reset.
- [x] Authoring: a new 2D/3D grid bundle, terrain/nav/camera, unique UUIDs, a no-overwrite transactional publish.
- [x] Editor: a world wizard, runtime entity edit/adopt/delete, checkpoint save/restore/reset and status.
- [x] Demo: F5/F9, a state on re-entry and a disk restore oracle, real screenshots.
- [x] Regressions, an ASan stack analysis, export, documentation, main push.

Serialized component state is kept; arbitrary C++ stack/heap and physics replay are not a checkpoint.

Evidence (2026-11-11): 792/792 unit (20 new), 10/10 headless CTest, 12/12 render reference,
490/490 editor smoke steps. In the 2D/3D Player from 540 frames the edit/delete/adopt/unload/return/disk restore
oracle; in both editors the 6-cell wizard, no-overwrite, the project link, generated Play and the actor restore; the
generated 3D project native export/run/screenshot/relocation. 6/6 tests in a separately instrumented ASan/UBSan probe
for the checkpoint store/writer (not a full engine sanitizer run), Clang 18 syntax-only -Werror 13 files.
The earlier CI 625d750 passed GCC/Clang/UBSan/coverage; Mesa's 2×56-byte drawArrays JIT retention in ASan was
separated as an artifact by its stack with a narrow vendor suppression. The new remote CI is watched separately after
the push. Evidence: `build-review/CheckpointPackage/`. A serialized component snapshot is not an arbitrary C++
heap/physics replay; the restore validation and the CPU part of the capture give no hard frame deadline yet.

### Finished large package — residency, terrain and navigation integration

- [x] CI: a camera tour that works in every CMake module output location; checking the vendor leak exception.
- [x] Resources: texture/model/material/mesh LRU, live references and async load pins, bounded cleanup.
- [x] Memory: real RSS and tracked GPU allocations separately; a new admission control with high/low watermarks.
- [x] Terrain: a world-space generator, joining borders of different resolutions and protecting outer patch LOD.
- [x] Navigation: joining the loaded cells' grids through a bounded mosaic, hole and height rules.
- [x] Editor/runtime: cache and pressure diagnostics, a manual trim, an updated demo and real screenshots.
- [x] Tests: cancellation/pinning/pressure/seam/path regressions, build/export, main push.

This package is not a physical VRAM hard cap, a polygon navmesh or a claim of parity with Unreal.

Check (2026-11-10): 772/772 unit, all 10 headless CTest, 12/12 render reference;
OpenGL 4.1 real texture/model/material/mesh residency and 32 cycles; 3D/2D camera tours from 660 frames,
6 cross-cell complete paths in each; terrain pair editor Undo/Redo; 490/490 editor smoke steps;
native export, screenshots and relocation of the two demos. The Clang 18 syntax-only `-Werror` check passed too.
The asset resolve cursor not being reset on reload was fixed; the entity count was tested with 3→1→4→2→0→3.
The manual and automatic trims share one world tick allowance; the thumbnail/reimport LRU metadata is cleaned up too.
Evidence: `build-review/ResidencyPackage/Final/`. The remote compiler/ASan matrix is checked separately in the CI after the push.

The next P3 work: incremental nav bake, terrain normal seams, HLOD, floating origin and versioned gameplay/physics persistence. The serialized cell checkpoint workflow is done.

### Finished package — packed foliage GPU culling and LOD

- [x] Up to 65536 static instances in one component, two model/mesh LODs, tint and view distance.
- [x] OpenGL 4.3 compute + SSBO compaction + indirect draw; an OpenGL 4.1 CPU culling/instancing fallback.
- [x] LOD hysteresis, conservative merged bounds, no reupload of the static input while the camera moves.
- [x] Asynchronous GPU visibility counts and CPU/GPU buffer sizes; no blocking readback on the rendering path.
- [x] Scene/prefab/copy/undo serialization, model UUID references, the inspector and placing on the parent terrain surface.
- [x] A CPU/GPU image/count oracle, a final check of shadow/picking/wire and both capability states.
- [x] A dense demo scene, screenshots, export and regressions.

V1 limits: static opaque/cutout meshes, 128 parts/LODs; one picking ID per component. The shadow conservatively draws every instance in the near LOD; there is no separate GPU shadow culling. The visible selection outline comes from the ID buffer; a packed occluded silhouette is not drawn. Collider/gameplay entities are not created per instance. HLOD/streaming is a separate phase.

Check (2026-11-10): 711/711 unit, 12/12 reference render; the GL 4.3 compute and GL 4.1 CPU oracle (4096 instances, five cameras, a 0 pixel difference in the main images), multi-part draws, shadow/picking/wire and the backend switch passed. In the first control view 54756 triangles instead of 491520; the camera did not change the number of source uploads. FoliageWorkshop with 4096 instances/5 entities passed save/reload, export, a Player screenshot and relocation; two OBJs and 12 PBR maps went into the package. That a legacy terrain reproject does not move the packed root was checked in a CPU regression. The SDK 2 test and the runtime without UI passed. Evidence: `build-review/PackedFoliage/`. Mesa llvmpipe results are not an estimate of physical GPU speed.

### Finished large package — working 2D/3D world streaming

The result of one package: cells loaded by the camera, physics/script lifecycle in a shared scene,
editor control and an exportable 2D/3D demo. The items below were closed with local test evidence; the remote sanitizer/compiler matrix is checked separately after the push.

- [x] Runtime: radius/hysteresis, layer/dependency closure, slot/entity/declared memory admission.
- [x] Bulk import: the runtime state starts once every object is ready; shared 2D/3D physics, unload and rollback.
- [x] Host integration: Project.StreamingWorld, SceneManager frame boundaries, Stop/Single/restart safety.
- [x] Editor: editing the world manifest/policy, a grid of states, runtime statistics, retry/preview.
- [x] A 2D and 3D multi-cell sample, a camera walk and a boundary/cancel return regression.
- [x] Export: the manifest and its scene/asset closure, a Player test moved to a clean location.
- [x] CPU/GL/editor/runtime/SDK tests, screenshots and a final report; all pushed to main as one package.

### Finished package — world cells and the streaming foundation

- [x] A common world-cell manifest for 2D tilemaps and 3D terrain, stable UUIDs and dependency bounds. `.zworld` v1 metadata, bounded parsing, atomic save and safe scene paths; editor/runtime streaming not connected yet.
- [x] Bounded scene IO/parse off the main thread; generational requests and CPU staging cancel/discard. `WorldCellLoader` v1: dependency closure, bounded input admission and symlink-safe worker IO. Active scene unload, texture decode and renderer/residency integration in later items.
- [x] A soft build/resolve and activation/unload count budget on the main thread; physics/script lifecycle in a shared scene.
- [x] Strict resource/file budgets for texture/model decode/upload; hard frame-time acceptance. `Streaming.AssetStreaming` (off by default): decode/parse and the mip chain on `WorldAssetStager` workers, the GPU upload queued under the `AsyncLoader` frame budget; a cell activates once all its assets are on the GPU.
- [x] A RAM/VRAM residency limit; fast camera movement, return, cancellation and missing/damaged cell regressions. `EvictOnPressure`: a limit on the observed RSS / requested GPU bytes (not physical VRAM), rejecting in advance by the measured GPU size.
- [x] A multi-cell demo, save/reload/export, a repeatable camera path and RSS/frame-time reports. `Projects/WorldGallery3D`, `.ztour` + `ZAMIN_WORLD_TOUR` / `ZAMIN_WORLD_REPORT`, `Tools/make_world_gallery.py`.

Evidence (2026-11-23, GCC Debug, Mesa llvmpipe 2 threads, 4 CPUs, xvfb; details — [Quality.md](Quality.md#the-world-asset-streaming-package--2026-11-23)):
1368/1368 unit (15 new; `--quick` 1356/1356), CTest without GUI 17/17, docs 2/2, world/residency GUI CTest 11/11
(with three new `WorldAssetStreaming_*` stages and an extended `AssetResidencyGPU`), 12/12 reference render,
EditorSmoke; the build without warnings.
**Budget** (`ctest -R WorldAssetStreaming_budget`, 24 cells, a 2048² texture = 16 MiB with mips, a model of 5184
triangles, a 1080-frame tour, 2 MiB / 8 uploads per frame): 0 image decodes / 0 model parses on the main thread (118 / 8
on the old path); bytes sent per frame ≤ 2 097 152 (the peak is exactly that), calls ≤ 2, 0 uploads larger than the
budget, the staging peak 256 MiB = the limit; the streaming work in one frame (world update + upload) at most
**13.99 ms**, 88.66 ms on the old path (gate: ≤ 30 ms and less than half of the old path; the old path does not pass the
gate — 108 frames above 30 ms); frame time p50/p95/p99 32.79/44.57/51.53 ms, on the old path 35.57/68.17/103.11;
1 frame 40 ms longer than the median (without streaming work) / 39 on the old path (38 of them with streaming). With an
8 MiB cell budget 17 stagings were rejected by name, 0 uploads. GL: the worker mip chain differs from
`glGenerateMipmap` by 0, a half-loaded texture is never visible, a cancelled upload returns its GPU allocation.
**Limits** (`ctest -R WorldAssetStreaming_residency`): a two-lap tour without limits (2160 frames, a fast camera, an
empty area, a return) — 105 activations / 97 unloads / 30 cancellations, 0 failed, the requested GPU in the empty area
342 → 342 MiB, the lap peak 406 → 406 MiB, the RSS peak 819 → 819 MiB; a GPU limit of 325.8 MiB — the peak
325.6 MiB, 23 cells rejected without loading, 2 evicted, up to 7 active; an RSS limit of 667 MiB — 115 pressure
frames, the heap returned to the OS 37 times, the peak 709 MiB (+42 MiB: based on observation, not a strict ceiling),
up to 9 active; a damaged world — exactly 2 failed for the missing and the damaged cell, the damaged/missing texture and
the damaged model reported 9 times, the rest active; 0 decodes/parses on the main thread in every tour.
**Demo** (`ctest -R WorldAssetStreaming_demo`): WorldGallery3D is byte-for-byte identical to the generator's output;
two tours — the same camera path and 8/4/5/0/8 active cells at the rest points, 79 activations / 71 unloads; frame
time p50 66.86 / p95 98.11 / p99 111.84 ms, RSS 303 → 326 MiB, requested GPU 100 → 110 MiB; the editor's world panel
with a screenshot; the export 190 files / 52.4 MB, the same tour in the moved folder gives the same result (p50
69.91 ms, the RSS peak 295 MiB, 0 unread files); save/reload and the export plan in a unit test (24 cells, 144
entities). Evidence: locally in `build/WorldAssetStreaming/Final/`.

Limits (not marked as done): measuring a physical GPU and physical VRAM; llvmpipe is not a speed estimate.
The frame time gate bounds the streaming work — the frame in which the renderer compiled a shader variant for the first
time (337 ms, streaming work 0.2 ms) is outside it. The time to create/destroy GL objects is the driver's
(on llvmpipe one upload chunk up to 14 ms, 20.5 ms in the GPU limit tour). Decoding terrain layer textures, fonts,
animation clips and images inside models is not in the asset budget; meshes are not split (one larger than the frame
budget is rejected). The RSS limit acts after the fact. The remote compiler/sanitizer matrix was not run.

The acceptance that remains for packed foliage: individual instance authoring/Undo, textured billboard impostors, HLOD disk streaming, a physical GPU and an OS/driver matrix. GPU shadow-set culling and HLOD v1 are done in the SDK package below (the separate GPU culling/LOD/HLOD item).


### SDK protection, profiling and foliage authoring v1 — a finished package

1. **SDK/module compatibility:** a fingerprint of the SDK headers and toolchain, an ELF ABI record inside the module, a bounded parser before `dlopen`. An unmarked, damaged or mismatched module is rejected; the running module is kept. When an SDK header changed, the engine is rebuilt first.
2. **Regression and editor diagnostics:** matching/wrong/old/stripped ELFs, malformed bounds, constructors not being run, keeping the running module, build/hot reload through the SDK and export. The editor shows the state, with a screenshot.
3. **Profiling:** checking the existing CPU/frame/memory counters; GPU timer queries and a frame-time view; repeatable 2D/3D benchmark scenes. The VPS correctness and the physical GPU performance evidence are kept separately.
4. **Foliage:** measuring the existing instancing path; density/LOD/culling and re-placing objects on the surface after a terrain sculpt. Small and dense map tests, save/load/undo and screenshots.
5. **Wrap-up:** the relevant CPU/GL/SDK/editor/export tests; updating the documentation and the results. The next packages go on with streaming, gameplay authoring and the criteria for two finished games.

- [x] SDK fingerprint and bounded ELF preflight.
- [x] Keeping the running module/class registry on a failed reload, with precise diagnostics.
- [x] A real SDK/module/hot reload/export regression and screenshots.
- [x] Asynchronous GPU timeline profiling v1; the physical GPU acceptance is still open.
- [x] Foliage surface fitting v1: manual, automatic with the brush, offset/normal, lock/hidden, Undo/Redo.
- [x] Separate GPU foliage culling/LOD/HLOD and a dense scene benchmark.

GPU foliage culling/LOD/HLOD evidence (2026-11-27): one GL 4.3 compute pass per packed batch culls every instance (frustum + distance), picks its LOD, and writes two compacted sets (camera and shadow; near LOD from the front, far from the back) plus their indirect commands (`glDrawElementsIndirect`, base instance for the far part); the CPU path (GL 4.1 fallback, or the inspector's CPU choice) applies the same rules and streams buffers sized to what is visible. The shadow cascades draw the shadow set (instances inside a cascade's volume, per LOD) instead of every near instance. HLOD: a world X/Z cell grid (at most 4096 cells) baked into clump-pyramid proxies, deterministic (FNV-1a per cell), cached per cell and rebaked only where the instances changed, crossfaded with a 16-step dither (`u_LODFade` / a per-instance fade attribute), drawn with one `glMultiDrawElements` per step; proxies cast no shadow. Stats: per-LOD, shadow-set and HLOD counts, CPU cull time and the GPU pass time from timestamp queries in the inspector, the Statistics panel (`Foliage LOD/HLOD/culling`), `RenderStatistics::Format` and a `Foliage culling` profiler scope; the inspector has the CPU/GPU combo, LOD/view/HLOD distance, transition, cell and the LOD/HLOD debug colours. Tests: `ZaminTests --filter PackedFoliage` (9: HLOD bake determinism, one-cell invalidation and restore from the cache, the cell limit and fade steps, the new settings' sanitize/save/load/copy and legacy files, the CPU box test against `Frustum`); `ctest -R FoliageGPUCulling` (the GL oracle under xvfb; with HLOD and a sun over five cameras the CPU and GPU paths agree on the camera and shadow counts per LOD, the replaced instances and drawn/fading cells, 0 pixel difference; the timestamp result, the debug colours, a one-cell rebake; also with `MESA_GL_VERSION_OVERRIDE=4.1` on the CPU fallback); `ZaminRenderTests` 15/15 with the new `Foliage`, `Foliage_CPU` (same reference, 1 differing pixel) and `FoliageLODDebug`; `Tests/foliage_editor_test.py` types the LOD/HLOD fields, switches to CPU culling, saves, undoes and checks the debug colours in the real editor. Dense benchmark (`ZaminBench --foliage`, `ctest -R BenchFoliageSmoke`, baseline `Tests/Baselines/foliage-release.json`): 1 048 576 instances in 16 components on Mesa llvmpipe, Release, 1280x720, 60 frames - with HLOD the CPU path 543.5 ms / frame, cull 25.1 ms, 41 draw calls; the GPU path 476.8 ms, cull 21.6 ms CPU (llvmpipe runs the dispatch on the CPU) / 16.9 ms GPU, 67 draw calls; both 454K triangles, 717 cells replacing 987 136 instances; without HLOD 2 911 / 2 870 ms, cull 80.4 ms CPU against 49.6 ms GPU, 8.71M triangles; the final counts were identical on both paths ([Benchmarks](Benchmarks.md#dense-packed-foliage-zaminbench---foliage)). Limits: llvmpipe numbers, not a physical GPU; the GPU path issues indirect draws for empty sets; the shadow set is one union of the cascades (not per cascade or per receiver); proxies are untextured clumps without impostor cards; the bake lives in memory (no disk cache/streaming); the dithered crossfade makes the editor's selection outline busy in the transition band.

Check (2026-11-10): 707/707 unit, 12/12 reference render, GL 4.1 terrain and GPU timer tests; the SDK native load + relocated SDK manifest tests; a real editor hot reload; the Platformer2D export/screenshot/relocation with 26 script classes; rejecting an export with a mismatched module; the runtime without UI. The foliage editor updated 89 roots, Undo/Redo passed. In the GUI the constructor of a native legacy candidate did not run; an unchanged candidate did not fall into endless retries. The reports are in `build-review/SDKQuality/`.

**Work after the SDK package:** packed foliage GPU/CPU culling and model LOD v1 were done in the package above. The dense scene export and the correctness oracles are recorded there. A physical GPU frame-time/RSS budget still needs a separate acceptance; the world phase after it is async tile streaming.

### P1 — a finished work package

- [x] Terrain render chunks of 32×32 cells and distance-based LOD.
- [x] Neighbouring chunks differ by at most one level; fine edges are geometrically matched to the coarse edge.
- [x] A 15% hysteresis at the LOD boundary; full detail in an orthographic view.
- [x] Only the chunks affected by a changed height and normal are rebuilt.
- [x] Painting does not rebuild the geometry; collision/raycasts stay at the full heightfield precision.
- [x] The LOD settings are kept through scene/prefab/copy/undo; the editor shows the real counters.
- [x] Large 2D tilemap visibility checks regions hierarchically and keeps the earlier row-major order.
- [x] Rotated, mirrored and perspective 2D cases match the full-check oracle.
- [x] CPU algorithm tests, a real GL cache/sculpt test, save/reload, render and editor regressions.
- [x] A repeatable measurement by the number of geometry and AABB checks; not mixed with FPS.

### Visual Script — a user request, 2026-11-10

- [x] The `.zgraph` format, a typed catalogue, validation and an indexed bounded VM.
- [x] A node/pin/wire editor, search, pan/zoom/fit, error diagnostics, atomic save, a 64-state undo/redo.
- [x] Lifecycle, collision/trigger/UI/AI events; input, transform, 2D/3D physics, prefab/audio/animation/UI actions.
- [x] Variables, instance overrides, condition/sequence/loop/break/delay and named functions.
- [x] The native host, a bridge to reflected C++ fields, messages; running without a C++ module.
- [x] Breakpoints, pause/step/continue, watches and a bounded execution trace.
- [x] Asset registry / content browser / inspector / reference scan and rename integration.
- [x] The `VisualWorkshop` 2D/3D sample and the [guide](VisualScripting.md).
- [x] Functions with parameters/return values (node library calls, double-click open, return-path diagnostics), array and enum pins (graph-local and reflected C++), collapse to function, `.zgraph` format 2 with version 1 migration.
- [x] Graph-local struct pins v1: struct definitions with typed fields (enums and arrays included), Make/Break/GetField/SetField, struct variables/locals/parameters/results, struct arrays, `.zgraph` format 3 with version 1/2 migration, a Structs editor with wire-keeping renames and undo.
- [x] Structs inside structs (cycle and depth checks, nested Make/Break/GetField/SetField, renames, field-by-field nested editing), enum arrays (`ArrayEnum` through the array nodes, ForEach, Constant, variables, parameters and locals) and graph-local inline macros (exec/data tunnel pins, latent flow, per-instance expansion with diagnostics on the instance, a Macros editor with open/rename/delete and undo), `.zgraph` format 4 with version 1-3 migration.
- [x] Curve pins (a `Curve` value type of keys with constant/linear/cubic interpolation and tangents for variables, parameters, locals, macro pins, struct fields and literals; Curve.Evaluate/KeyCount/Length/AddKey; an inline curve editor with a preview, draggable keys and undo) and graph inheritance (a parent `.zgraph` whose variables, functions, enums, structs, macros and event handlers the child inherits; function and event overrides with Parent.Call/Parent.Event; cycle, missing-parent, conflict and signature diagnostics; parent edits and renames followed on load; inherited members marked read-only in the editor, Set parent and Override with undo), `.zgraph` format 5 with version 1-4 migration.
- [x] Full native method reflection (`ZM_METHOD` / `ZM_PURE_METHOD` / `ZM_FUNCTION`, C++ enums and structs with `ZM_SCRIPT_ENUM` / `ZM_SCRIPT_STRUCT`; `Native.Call` / `Native.Pure` nodes from the node library; signatures kept in the graph, bound to the live methods again after every module load or hot reload, a changed or missing method marked on its nodes with Refresh from C++), the bytecode cook (every graph compiled at export to validated, versioned bytecode with a content-hash cook cache; the Player runs it, the editor the sources) and large graph profiling (a per-node/per-function VM profiler with hot paths, the Visual Script profiler panel, a heat overlay, a `Visual Script` profiler scope, canvas culling, `ZaminBench --graphs`), `.zgraph` format 6 with version 1-5 migration. Native code generation from graphs is not done.
- [ ] RPC/replication nodes (needs a networking layer).

Visual Script evidence (2026-11-26): `ZaminTests --filter Blueprint` and `VisualGraphReadsAndWritesReflectedCppEnums` cover the VM, validation, format 2 round trips, the version 1 migration, enum checks and collapse; `Tests/visual_graph_editor_test.py` creates a function in the real editor, adds a parameter, calls it from the node library, undoes/redoes, opens it by double-click and runs it in the Player; VisualWorkshop's `RankOf` returns a `Rank` enum (Silver in 2D, Gold in 3D) in the Player and the editor. Struct evidence (2026-11-26): `ZaminTests --filter BlueprintStruct` and the format 3 migration test; `Tests/visual_graph_editor_test.py` creates a struct, adds and renames fields, builds it with Make/GetField from the node library, renames the wired field (wires kept), undoes/redoes and runs it in the Player; VisualWorkshop's `Summary` (struct `Result`) prints `BLUEPRINT_SUMMARY` in the Player and the editor. Nested struct, enum array and macro evidence (2026-11-27): `ZaminTests --filter Blueprint` (`BlueprintNestedStruct*`, `BlueprintEnumArrays*`, `BlueprintMacro*`, `BlueprintFormatVersion4AndOlderMigration`, `BlueprintFormatVersion3StructFilesLoadUnchanged`); phase 4 of `Tests/visual_graph_editor_test.py` gives a struct field a struct type and edits its nested default, adds an `ArrayEnum` variable, creates a macro, adds and renames an output (undo/redo), opens it, edits its body from the node library, uses it from the node library, renames it and opens it by double-click, then runs it in the Player; VisualWorkshop's `Report` (a nested `Result` and an enum array) and its `Announce` macro print `BLUEPRINT_REPORT` in the Player and the editor. Curve and inheritance evidence (2026-11-27): `ZaminTests --filter Blueprint` (`BlueprintCurve*`, `BlueprintInheritance*`, `BlueprintFormatVersion5AndOlderMigration`, `BlueprintFormatVersion4FilesLoadUnchanged`) covers curve evaluation and shape checks, the curve nodes and curve pins in structs/functions/locals/macros, inherited members, function/event overrides over three levels, diagnostics (conflicts, signatures, missing parent, cycles, errors in a parent's node) and a parent's renames and edits followed on load; phase 5 of `Tests/visual_graph_editor_test.py` sets a parent (undo/redo), checks the inherited variable is listed and not saved, adds a `Curve` variable and edits its keys (add, Time/Value fields, interpolation, delete, a drag in the preview; undo/redo), overrides an inherited function (undo/redo), extends the override with the curve from the node library, overrides the inherited Start event and runs the child in the Player; VisualWorkshop's `BlueprintChild` (a child of `BlueprintBase`, overriding `Bonus`) evaluates the parent's `Falloff` curve and prints `BLUEPRINT_INHERIT curve=2.000 bonus=30.00 keys=3` (2D) / `curve=3.375 bonus=43.75 keys=3` (3D) in the Player and the editor.

Native method, bytecode and profiling evidence (2026-11-27): `ZaminTests --filter Blueprint` (`BlueprintNativeMethodsReflectTypedSignatures`, `BlueprintNativeMethodsRunPureExecStaticStructsAndArrays`, `BlueprintNativeMethodErrorsAreClearAndStopTheGraph`, `BlueprintNativeMethodsSurviveHotReloadChanges`, `BlueprintNativeFormatVersionAndValidation`, `BlueprintGraphCookCompilesValidatesCachesAndKeepsParents`, `BlueprintBytecodeRunsLikeTheSource`, `BlueprintBytecodeRejectsWhatTheRuntimeRejects`, `BlueprintProfilerCountsNodesFunctionsAndHotPaths`, `BlueprintFormatVersion6AndOlderMigration`) covers the reflected signatures (instance, static and free; pure and exec; int/float/string/vector/entity/enum/struct/array values), running them in a scene, clear errors (a fraction into an int, a C++ exception, no live script on the Target), a module unloaded and re-registered with a changed signature (the graph still loads and keeps its wires, the binding problems name the change, Refresh keeps what fits, the host binds again on the new registry generation), format 6, paste, every kind of graph cooked to bytecode and run against its source, damaged/foreign/outdated bytes refused, the profiler's calls/inclusive/exclusive times, functions, hot path and merge, and the export's graph cook (rejected graphs and their parents kept as JSON, the cache, native warnings, the host running a cooked file); 1426/1426 quick unit tests. Phase 6 of `Tests/visual_graph_editor_test.py` adds `WorkshopStats.Score` from the node library's C++ section (undo/redo), rebuilds the VisualWorkshop module with a third parameter while the editor runs (the hot reload marks both Score nodes red, the graph still compiles and is unchanged; Refresh from C++ adds the `Extra` pin and keeps the wires; undo/redo) and runs it in the Player, then plays a 3000-node graph in the editor: 131 nodes drawn, the loop's `Add` the hottest node in the heat overlay, the profiler lists 906 nodes and the hot path and a row selects its node. VisualWorkshop's `NativeMethods.zgraph` (an execution method, two pure methods with a C++ enum, a free function) prints `BLUEPRINT_NATIVE score=36 medal=Silver hypot=5.00 awards=1` (2D) / `score=54 medal=Gold` (3D) in the Player and the editor (`Tests/blueprint_workflow_test.py`, 4 passes). `ctest -R ExportCookedTest` exports VisualWorkshop: 8 of 8 graphs cooked (47 KB → 10 KB), the exported Player prints the `BLUEPRINT_*` lines from the cooked graphs (the native ones through the Release module), the second export takes all 8 from the cache, the Export report window lists them. `ZaminBench --graphs` (Release, 4 vCPU Xeon @ 2.10 GHz, about 4000 nodes, `Tests/Baselines/graphs-release.json`): the cooked file is 10-17x smaller than the source and loads the VM in 1.9-2.9 ms against 26-76 ms; fixing the large-graph problems it found took loading a file from 81-108 ms to 15-18 ms, validating the 31-function graph from 43.6 to 13.5 ms and running the three cases from 2.8/13.4/23.1 ms to 1.1/10.0/16.7 ms; the editor's canvas culling took a 4000-node graph from 149 ms (562 772 vertices) to 19 ms (17 932) of canvas time a frame (Debug, llvmpipe). Native code generation from graphs and RPC/replication are not done.

### The built-in C++ code editor — 2026-11-10

- [x] Tabs inside the editor, a source list, C++ colours, a monospace font, line numbers and auto-indent.
- [x] Find/replace, undo/redo, a catalogue of 265 API names/signatures, Ctrl+Space and gameplay snippets.
- [x] Opening a source from the inspector/content browser/compiler in the built-in window; read-only SDK files.
- [x] Save-all before a build, F7 build/hot reload, error line/column navigation.
- [x] Atomic save, external change conflicts, UTF-8/BOM/CRLF, bounded undo and crash recovery.
- [x] 9 file-protection tests, 691 unit tests in all, a real keyboard/build/error/recovery GUI test.
- [ ] Semantic completion, go-to-definition, project rename and live type diagnostics through clangd/LSP.

### P2 — GPU terrain materials, 2026-11-10

- [x] Instead of a 512×512 runtime bake, four linear albedo layers on the GPU and an RGBA32F control matching the resolution.
- [x] A tangent normal, ORM, normal strength, roughness/metallic/occlusion and metre-based UVs for every layer.
- [x] Wired into the existing lighting/shadow/fog/debug/picking/wire paths; ordinary mesh materials stay separate.
- [x] TextureLibrary import/cache/reload, safe asset paths, a fallback for missing maps and control counters.
- [x] Serializer asset handles, legacy defaults and editor controls.
- [x] 696/696 CPU, 12/12 render, GL 4.1 pixel/cache tests, editor and MapWorkshop export/relocation checks.
- [x] Packed foliage instancing/culling and two model LODs v1 (the package above).
- [x] Atlas/cook, terrain tile streaming and 2D content workflow work (v1, 2026-11-27).

P2 evidence: the 12 source PBR textures went into the package through export reachability; the exported Player passed 12 measurement frames, a screenshot and a move to another folder. The editor showed 67600 bytes of control, 1 upload, 0 missing maps. The GL test confirmed the LOD UV match, normal/ORM/AO, picking/wire, the native mesh binding, reimport, safe paths and no upload over 120 validation frames. Losing the render target on a shadow atlas resize and a paint float oscillation were fixed. A GPU test step was added to CI; in this session the tests ran locally on a VPS/Mesa llvmpipe. A physical GPU, Windows/macOS and full professional parity are not confirmed yet.

Atlas/cook, tile streaming and 2D content evidence (2026-11-27): a deterministic MaxRects atlas packer with clamp/wrap
bleed and mip-safe alignment (`Asset/TextureAtlas.h`); `.zatlas` sprite atlas assets made from a content browser folder,
rebuilt by the editor when a source changes on disk with the page's UUID, frame names, pivots and borders kept; alpha
slicing and re-slicing that keeps references in the Sprite editor; atlas frames used by Sprite Renderers, a Sprite
Animation, `.zsanim` clips made from a sequence and grid pages as Tilemap tilesets. The export cooks textures into ZTEX
(lossless, halved to a size limit or by the mips every quality level skips, written at the source's path when smaller or
resized) with a content-hash cook cache, re-packs stale atlas pages, writes `cook-manifest.json` with the atlas UV table
and lists every texture in the report; the Player reads cooked data, the editor the sources. `TiledTerrainComponent`
streams a `.zterrain` tile set (heights, control, foliage and 4 decimated LOD levels per tile file) around the camera on a
background reader within a hard memory budget (farthest tiles coarsened, then dropped), stitches borders between tiles
of different LOD with the existing patch stitching (`EdgeLOD`, no cracks, no skirts), makes colliders for near
full-detail tiles in play, sculpts and paints across tile borders with shared samples reconciled and saved, and migrates
a single terrain (exactly or resampled). Stats in the inspector, the Statistics panel and a profiler scope. Checks: 1417/1417
quick unit tests (15 new: packer, bleed/mips, slicing, atlas reimport, ZTEX, cook cache, export cook, tile files, budget/LOD,
async reads, seams, sculpt, migration), `ctest -LE gui` 17/17, docs 2/2, `Tests/atlas_terrain_editor_test.py` (alpha
slicing + pivot, atlas from a folder + automatic rebuild, LOD colours, a stroke across tiles saved with equal shared edges
on disk), `Tests/cook_export_test.sh` (StreamingWorkshop: 3/3 textures cooked, 512 -> 256 px terrain textures 420 KB ->
109 KB each, the exported Player ran 60 frames, a screenshot and a move; the second export 3/3 from the cache), and
`ZaminBench --terrain-tiles` (Release, llvmpipe: streaming update 0.86 ms async vs 1.31 ms sync per frame, 12 MB budget
never exceeded, 0 cracks; `Tests/Baselines/terrain-tiles-release.json`). Not done in v1: GPU block compression, multi-page
atlases, normals shared across tile borders, tile edits in the scene Undo history, impostors beyond the load radius; frame
times are Mesa llvmpipe, not a physical GPU.

### Criteria for reaching Unity / Unreal / Godot quality

Parity with these engines is not decided by a single feature or by the amount of code. For every area the work a user can do, a repeatable test and the open limits are shown; no unconfirmed percentage is given.

| Area | State in Zamin | The next evidence for acceptance |
|---|---|---|
| 2D | Tilemaps, sprites, animation, Box2D, UI, large-map visibility, sprite atlases and alpha slicing exist | A finished 2D game; texture residency, input/UI and a stress profile |
| 3D maps | Heightfield authoring, collision, patch LOD, GPU PBR layers, tiled terrain streaming with stitched LOD borders | A map with real assets and a physical GPU frame-time budget |
| Gameplay authoring | A C++ editor and a typed visual graph with functions, arrays, enums and structs exist | Curve types, macros, LSP, a large graph/debug workflow |
| World | Render chunks and a bounded world-cell manifest exist; disk streaming is planned | Async load/unload/cancel with bounded RAM/VRAM and a large world test |
| Team networking | Multiplayer production acceptance not done yet | Authoritative replication, latency/loss/reconnect tests, security and monitoring |
| Product quality | Linux tests and an export workflow exist | A native SDK ABI gate exists; an OS/driver matrix, crash/recovery and long sessions |

The official sources for the comparison criteria: [Unity Terrain Layer](https://docs.unity.com/en-us/engine/6000.6/manual/creating-environments/script-terrain/terrain-textures/class-terrain-layer), [Unreal Landscape Materials](https://dev.epicgames.com/documentation/unreal-engine/landscape-materials-in-unreal-engine), [Godot MultiMesh](https://docs.godotengine.org/en/stable/classes/class_multimesh.html). The current GPU terrain phase is not a claim of parity with every feature of these products.

### The work needed for P2–P8

**2D:** a tile render cache and dirty regions, large atlases and texture residency, animated tile updates,
terrain/auto-tile borders, collision chunk rebake, navigation, sprite/animation/UI profiling. The existing culling,
layers, auto-tiles, Box2D, UI and animation are not rewritten; their weaknesses are strengthened with evidence.

**3D terrain and foliage:** checking the existing GPU layers with large assets, texture arrays/material variants,
screen-space error LOD and smoothing the transitions, joining terrain tiles, import/export formats, road/spline tools,
GPU instancing/culling, density masks, asset LOD/HLOD, re-placing foliage on the surface after edits,
async collider cooking and navmesh updates. Voxels/caves — a system separate from the heightfield.

**Common content:** keeping references on reimport, cook and deterministic cache keys, damaged files,
compressed packages, locale/shader variants, asset pipeline rollback and versioned serialization; the SDK ABI gate v1
was added. When engine components change, checking/rebuilding old C++ modules must be automatic and understandable.

**Gameplay:** testing the character/controller, camera, input remap/gamepad, AI/navigation, animation state/transition,
the root motion/IK/retarget workflow, the audio mixer/streaming, timeline/VFX and save systems in one game
flow. A 'present' mark for an existing feature is not treated as production quality.

**Multiplayer:** an authoritative server, a bandwidth budget, snapshots/interpolation, prediction/reconciliation,
physics ownership, replication relevancy, matchmaking/sessions, deciding the host migration requirement separately,
reconnect and account services. The transport and the threat model are chosen first; the client is not given the
server's authority.

**Editor/launcher auth:** following the earlier requirement, a mandatory login with a single Zamin account is planned.
Not embedding a server secret in the native client, browser-based PKCE, the OS secret store, session revocation and
the behaviour when the service is down are set out explicitly. This is not part of the engine core's render/physics loop.

**Performance and release:** 2D/3D benchmarks pinned to a seed/scene/settings version; CPU/GPU frame time,
p95/p99, RAM/VRAM, loading and draw/triangle counters. The 60 FPS/16.7 ms target is checked once a reference physical
GPU and a scene budget are chosen. A VPS llvmpipe test is not evidence of real GPU performance.
Every phase passes the correctness, sanitizer/recovery, regression, export and documentation gates.

The terrain components in Unreal and their LOD unit — the comparison source is the [Epic Landscape technical guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-technical-guide-in-unreal-engine).
The idea of loading disk cells by their proximity to a source —
the [Epic World Partition guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine).
The Zamin P1 render chunks are not disk streaming; that is done separately in P3.

---

## M01 Scripting and gameplay API

- [x] 001. An inspector for the EntityRef field (drag in from the hierarchy, clear)
- [x] 002. An EntityRef serialization test
- [x] 003. An Invoke/InvokeRepeating/CancelInvokes test
- [x] 004. A test of Tween and the easing functions
- [x] 005. An input action map (in the project settings: binding keys/gamepad/mouse)
- [x] 006. Input::IsActionPressed / IsActionJustPressed / IsActionJustReleased
- [x] 007. Input::GetAxis (negative/positive key + gamepad axis, deadzone)
- [x] 008. JustPressed/JustReleased for the keyboard and mouse (the state between frames)
- [x] 009. SaveData (PlayerPrefs): saving Int/Float/String/Bool, the file ~/.local/share/zamin/<project>
- [x] 010. A SaveData test
- [x] 011. A Random API (seed, Range, Float, Int, Bool, InsideCircle, OnSphere)
- [x] 012. A tag (Tag) system: FindEntitiesWithTag, HasTag
- [x] 013. Messages/events: SendMessage(name), Broadcast, an event bus between scripts
- [x] 014. GetComponentInChildren / GetComponentInParent / GetChildren
- [x] 015. Caching for FindEntity (a fast name->entity lookup)
- [x] 016. Script lifecycle: OnEnable/OnDisable/OnStart (before the first Update)
- [x] 017. OnTriggerBegin/OnTriggerEnd (a separate callback for sensors)
- [x] 018. Script fields: enums, and ZM_FIELD_COLOR for colors (glm::vec4 as a color)
- [x] 019. Tooltip/range attributes for script fields (ZM_FIELD_RANGE)
- [x] 020. A new script template: with every callback and examples

## M02 Editor UX I

- [x] 021. Selecting several entities (with Ctrl/Shift) in the hierarchy
- [x] 022. Deleting/duplicating several selected entities together
- [x] 023. Moving several selected entities together with the gizmo
- [x] 024. Reordering by drag-drop in the hierarchy (dropping between siblings)
- [x] 025. A Transform context menu: reset, copy, paste
- [x] 026. Snapping settings (step: move/rotate/scale) in the toolbar
- [x] 027. Grid settings (size, color, visibility)
- [x] 028. Camera bookmarks (Ctrl+1..5 to save, 1..5 to return)
- [x] 029. Frame All - focus on the whole scene
- [x] 030. Duplicating an entity in the viewport (dragging with Ctrl+D, Alt+drag)
- [x] 031. Reordering components in the inspector (up/down)
- [x] 032. Copying a component and pasting it onto another entity
- [x] 033. Remembering the collapsed state of components in the inspector
- [x] 034. Visibility (eye) and lock buttons in the hierarchy
- [x] 035. Locked entities are not selected in the viewport
- [x] 036. Moving files into a folder by dragging in the content browser
- [x] 037. A list view in the content browser
- [x] 038. A filter by file type in the content browser
- [x] 039. Importing a file into Assets by dropping it onto the window from outside
- [x] 040. The selected entity's information in the status bar

## M03 Active/inactive entities

- [x] 041. An Active flag on TagComponent
- [x] 042. Activity through the hierarchy (inactive parent -> the children too)
- [x] 043. Inactive entities are not drawn
- [x] 044. Inactive entities take no part in physics (the body is removed)
- [x] 045. The scripts of inactive entities get no Update
- [x] 046. A SetActive() script API
- [x] 047. The OnEnable/OnDisable calls
- [x] 048. An Active checkbox in the inspector
- [x] 049. Inactive entities look dimmed in the hierarchy
- [x] 050. Serialization and tests
- [x] 051. The particle system stops when inactive
- [x] 052. Audio sources stop when inactive
- [x] 053. The Animator is not updated when inactive
- [x] 054. UI elements are not shown and cannot be pressed when inactive
- [x] 055. Inactive cameras do not count as the primary camera
- [x] 056. Inactive lights are not taken into account
- [x] 057. Tilemap collision turns off when inactive
- [x] 058. The character controller turns off when inactive
- [x] 059. Prefab instances keep their active state
- [x] 060. Activity tests (hierarchy, physics, scripts)

## M04 Asset system

- [x] 061. AssetHandle (UUID) and AssetRegistry
- [x] 062. .zmeta files (a UUID and import settings for every asset)
- [x] 063. Detecting asset types (texture, model, audio, scene, prefab, font, hdr, material)
- [x] 064. A file watcher - reloading changed assets
- [x] 065. Reloading textures (hot reload) in the editor
- [x] 066. Reloading models
- [x] 067. Texture import settings: filter, wrap, mipmap, sRGB
- [x] 068. An inspector for texture import settings
- [x] 069. Model import settings: scale, up axis
- [x] 070. Updating references when an asset is renamed (through the UUID)
- [x] 071. Showing an asset in the inspector when it is selected in the content browser
- [x] 072. A texture preview in the inspector (size, format)
- [x] 073. A model preview (submeshes, triangles, the list of animations)
- [x] 074. An audio asset preview (duration, a play button)
- [x] 075. Finding unused assets
- [x] 076. A warning that an asset is in use before it is deleted
- [x] 077. Searching assets by folder (the full path)
- [x] 078. Asset registry serialization and its tests
- [x] 079. Thumbnail caching in the content browser
- [x] 080. Printing asset import errors to the console

## M05 Materials (asset)

- [x] 081. The .zmat material asset format
- [x] 082. Loading/saving material assets
- [x] 083. A material asset reference in MeshRenderer
- [x] 084. A material editor in the inspector
- [x] 085. A material preview sphere (render-to-texture)
- [x] 086. A transparent (alpha blend) mode
- [x] 087. An alpha cutout mode (threshold)
- [x] 088. A double-sided flag
- [x] 089. An unlit shader mode
- [x] 090. Texture tiling and offset (vec2)
- [x] 091. Dragging a material from the content browser onto a mesh
- [x] 092. Creating a new material (the content browser menu)
- [x] 093. Hot reload of material changes
- [x] 094. Exporting glTF materials to .zmat
- [x] 095. Material instances (changing a color from a script without spoiling the shared asset)
- [x] 096. Sorting transparent objects back to front
- [x] 097. Emissive intensity and color in the material
- [x] 098. Material serialization tests
- [x] 099. A library of standard materials (metal, plastic, wood, glass)
- [x] 100. A thumbnail for material assets

## M06 Rendering I

- [x] 101. Spot lights (cone, inner/outer angle)
- [x] 102. A spot light inspector and wireframe view
- [x] 103. A wireframe radius view of point lights in the editor
- [x] 104. Raising the number of lights (32) and choosing the nearest ones (on purpose: not by distance alone but by the effect on the camera — intensity x (radius/distance)^2, the nearest one on an equal effect; in the README)
- [x] 105. FXAA anti-aliasing
- [x] 106. Render scale (an internal size factor)
- [x] 107. Automatic gamma/exposure (auto exposure, simple)
- [x] 108. A wireframe mode (editor)
- [x] 109. Lighting-only/normal/albedo debug views
- [x] 110. Frustum culling (for meshes)
- [x] 111. Frustum culling (for 2D sprites)
- [x] 112. Mesh instancing (the same mesh + material)
- [x] 113. Render statistics: the culled count
- [x] 114. Shadow quality settings (atlas size, PCF)
- [x] 115. Shadow bias settings in the scene settings
- [x] 116. Several suns with a directional light shadow (only one casts shadows)
- [x] 117. Setting up the sky gradient + the sun disc
- [x] 118. A chromatic aberration and film grain effect
- [x] 119. Tinting bloom by the light
- [x] 120. A rendering regression test (comparing headless screenshots)

## M07 Debug draw and visualization

- [x] 121. A DebugDraw API: Line, Box, Sphere, Arrow, Text (from scripts)
- [x] 122. DebugDraw with a duration
- [x] 123. DebugDraw only in the editor/Development mode
- [x] 124. Visualizing a raycast result
- [x] 125. Showing physics contact points
- [x] 126. Drawing the camera frustum in the editor
- [x] 127. Showing the light direction with an arrow
- [x] 128. Showing the audio source radius
- [x] 129. The character controller velocity vector
- [x] 130. Drawing navigation paths (with the next milestone)
- [x] 131. A gizmo size setting
- [x] 132. A selection outline (shader based, a smooth contour)
- [x] 133. The hovered entity is highlighted with an outline
- [x] 134. A bounding box display mode
- [x] 135. Drawing skeleton bones (animation debug)
- [x] 136. Showing normal vectors (debug)
- [x] 137. An FPS graph overlay in the viewport
- [x] 138. A draw call and triangle overlay
- [x] 139. Nested sections in the profiler (hierarchical)
- [x] 140. Debug draw tests

## M08 2D improvements

- [x] 141. A sprite sheet slicer (cutting by a grid, an editor window)
- [x] 142. A sprite atlas asset (named frames)
- [x] 143. 9-slice sprite rendering
- [x] 144. Sorting layers (named layers)
- [x] 145. A parallax layers component
- [x] 146. A 2D camera bounds and smooth follow component
- [x] 147. A camera shake API
- [x] 148. Tilemap: several layers
- [x] 149. Tilemap: auto-tile rules (4/8 neighbours)
- [x] 150. Tilemap: rectangle and flood fill tools
- [x] 151. Tilemap: per-tile collision flags
- [x] 152. Tilemap: one-way platforms
- [x] 153. Tilemap: animated tiles
- [x] 154. Tilemap: the palette when zooming in/out
- [x] 155. A sprite outline/flash effect (hit flash)
- [x] 156. Sprite color tint animation
- [x] 157. 2D lights (a simple 2D point light, additive)
- [x] 158. A pixel-perfect camera mode
- [x] 159. Sorting sprites by Y (top-down games)
- [x] 160. 2D tests

## M09 2D physics II

- [x] 161. Distance joint 2D
- [x] 162. Revolute (hinge) joint 2D (motor, limit)
- [x] 163. Prismatic joint 2D
- [x] 164. Weld joint 2D
- [x] 165. Wheel joint 2D
- [x] 166. Polygon collider 2D (a vertex editor)
- [x] 167. Edge/Chain collider 2D
- [x] 168. A physics material (friction/bounce) asset
- [x] 169. Collision layers and masks (2D)
- [x] 170. A one-way platform effector
- [x] 171. An area effector (wind/water)
- [x] 172. A collision filtering test
- [x] 173. OverlapCircle/OverlapBox queries 2D
- [x] 174. RaycastAll 2D
- [x] 175. An inspector and wireframe for joints
- [x] 176. Joint serialization
- [x] 177. A physical ragdoll sample (2D)
- [x] 178. Box2D sub-step settings
- [x] 179. A sleeping setting for 2D physics
- [x] 180. 2D physics tests

## M10 3D physics II

- [x] 181. Fixed joint 3D
- [x] 182. Hinge joint 3D (motor, limit)
- [x] 183. Distance joint 3D
- [x] 184. Slider joint 3D
- [x] 185. A convex hull collider (from a model)
- [x] 186. A static triangle mesh collider (from a model)
- [x] 187. Collision layers and masks (3D)
- [x] 188. OverlapSphere/OverlapBox queries 3D
- [x] 189. SphereCast/ShapeCast 3D
- [x] 190. RaycastAll 3D
- [x] 191. A continuous collision detection (CCD) flag
- [x] 192. A sleeping/wake-up API
- [x] 193. Physics material 3D
- [x] 194. Trigger callbacks 3D (OnTriggerBegin)
- [x] 195. The character controller pushes dynamic bodies (a force setting)
- [x] 196. A joint inspector and wireframe
- [x] 197. Joint serialization
- [x] 198. A 3D ragdoll (simple) sample
- [x] 199. Physics debug: contact points
- [x] 200. 3D physics tests

## M11 Audio

- [x] 201. Audio mixer groups (Master, Music, SFX, UI)
- [x] 202. Group volume and mute
- [x] 203. An AudioListener component (instead of the camera)
- [x] 204. A music crossfade API
- [x] 205. Music streaming (for large files)
- [x] 206. Sound pitch randomness (random range)
- [x] 207. A limit on sounds playing at the same time
- [x] 208. AudioSource: a Play/Stop/Pause script API
- [x] 209. Showing the AudioSource state in the inspector
- [x] 210. A Doppler effect (simple)
- [x] 211. An audio clip asset (duration, format)
- [x] 212. Sound fading (fade in/out)
- [x] 213. Sound effects: a low-pass filter
- [x] 214. Sound settings are saved with SaveData
- [x] 215. Audio tests (without a sound device)
- [x] 216. An audio profiler (the number of active sounds)
- [x] 217. An audio preview waveform in the editor
- [x] 218. A directional cone for 3D sound
- [x] 219. An ambient zone (sound inside a volume)
- [x] 220. Audio events (OnSoundFinished)

## M12 UI system

- [x] 221. A UI Image component (sprite, 9-slice)
- [x] 222. UI Slider
- [x] 223. UI Toggle (checkbox)
- [x] 224. UI Progress bar
- [x] 225. UI text input (input field)
- [x] 226. Vertical/horizontal layout groups
- [x] 227. Stretch anchors (stretching left to right)
- [x] 228. A scale that adapts to the screen size (reference resolution)
- [x] 229. UI events: OnHover/OnPress/OnRelease
- [x] 230. Keyboard/gamepad navigation for the UI
- [x] 231. UI animations (with tweens)
- [x] 232. Several font sizes (atlas quality)
- [x] 233. Text alignment (left/center/right, top/middle/bottom)
- [x] 234. Text wrapping (word wrap) by width
- [x] 235. Selecting and moving UI in the editor (a 2D gizmo)
- [x] 236. The UI hierarchy (relative to the parent UITransform)
- [x] 237. UI mask/clip (scroll view)
- [x] 238. Scroll view
- [x] 239. A dialog/menu sample (pause menu)
- [x] 240. UI tests

## M13 Animation II

- [x] 241. Property animation clips (.zanim): transform/color keyframes
- [x] 242. An animation timeline editor (an editor window)
- [x] 243. Keyframe interpolation (linear, step, bezier)
- [x] 244. Animation events (a script callback at a time)
- [x] 245. Root motion (glTF)
- [x] 246. Blend tree 1D (blending walk/run by speed)
- [x] 247. Animation layers (upper body) - simple
- [x] 248. Sprite animation clips as assets
- [x] 249. Showing the Animator parameters in the inspector at runtime
- [x] 250. A visual graph for the state machine (nodes)
- [x] 251. An animation preview in the editor (scrub)
- [x] 252. Skeleton: attaching an entity to a bone (attachment socket)
- [x] 253. Footstep sounds by animation speed, with an event
- [x] 254. Trimming animation clips (start/end)
- [x] 255. glTF morph targets (blend shapes) - simple
- [x] 256. Animation caching (performance)
- [x] 257. Support for several skins
- [x] 258. Animation tests (property clip, event)
- [x] 259. An animation sample (a door opening)
- [x] 260. Animation documentation

## M14 Particles II

- [x] 261. A color gradient (many points)
- [x] 262. A size curve
- [x] 263. Velocity over lifetime
- [x] 264. Texture sheet animation (flipbook)
- [x] 265. A cone emitter shape
- [x] 266. An emitter from a mesh surface
- [x] 267. Sub-emitters (an explosion on death)
- [x] 268. Collision with the ground plane (bounce)
- [x] 269. Noise turbulence
- [x] 270. Trail rendering
- [x] 271. More batching of particles on the GPU
- [x] 272. Particle sorting (distance to the camera)
- [x] 273. Prewarm (a state filled from the start)
- [x] 274. A table of emission bursts (bursts list)
- [x] 275. Simulation speed and local/world
- [x] 276. A library of particle presets (fire, smoke, sparks, rain, snow)
- [x] 277. Rain and snow effects (around the camera)
- [x] 278. Sections in the particle inspector
- [x] 279. Particle serialization tests
- [x] 280. A particle performance test

## M15 Scene management

- [x] 281. Additive scenes (several at the same time)
- [x] 282. DontDestroyOnLoad (persistent entities)
- [x] 283. Asynchronous scene loading (loading screen)
- [x] 284. Scene transitions (fade in/out)
- [x] 285. The list of build scenes (in the project settings)
- [x] 286. LoadScene by index
- [x] 287. Events when a scene is loaded/closed
- [x] 288. Speeding up the lookup APIs inside a scene
- [x] 289. Showing errors when a scene is loaded
- [x] 290. A stable UUID order for comparing scenes (diff)
- [x] 291. Scene metadata (description, thumbnail)
- [x] 292. Editor: a panel with the list of scenes
- [x] 293. Editor: restoring the editor state when the scene changes in play mode
- [x] 294. Global game state (singleton services)
- [x] 295. Scene version migration
- [x] 296. Showing the scene load time in the profiler
- [x] 297. Faster serialization for large scenes
- [x] 298. Splitting a scene into pieces (sub-scenes) - simple
- [x] 299. Scene management tests
- [x] 300. Scene management documentation

## M16 Editor II

- [x] 301. An editor color tint in play mode
- [x] 302. Game view resolution presets (16:9, 4:3, 1080p)
- [x] 303. A text filter in the console
- [x] 304. Grouping identical messages in the console (collapse)
- [x] 305. A settings window (Preferences): theme color, font size
- [x] 306. A window with the list of keyboard shortcuts
- [x] 307. Autosave and recovery
- [x] 308. A list of recent scenes
- [x] 309. An undo history panel
- [x] 310. Undo for the selection
- [x] 311. Saving/loading editor layouts
- [x] 312. Several viewports (side/top views)
- [x] 313. Buttons for the orthographic views (Top/Front/Right)
- [x] 314. An editor camera speed setting and FOV
- [x] 315. Showing entity names in the viewport
- [x] 316. A scene statistics panel (the number of components)
- [x] 317. Opening script files from the editor (an external editor)
- [x] 318. Showing compile errors with the file/line
- [x] 319. Fewer editor updates (an FPS limit when idle)
- [x] 320. Editor II tests (headless)

## M17 Project settings

- [x] 321. A project settings window
- [x] 322. An input map editor
- [x] 323. A tags and layers editor
- [x] 324. A physics layers matrix
- [x] 325. Quality settings (shadows, SSAO, bloom default)
- [x] 326. Player window settings (size, fullscreen, vsync, title)
- [x] 327. A game icon
- [x] 328. The start scene and the build scenes
- [x] 329. The company/game name and version
- [x] 330. Project settings serialization
- [x] 331. Project settings tests
- [x] 332. Graphics API and context settings
- [x] 333. Time settings (fixed timestep, time scale)
- [x] 334. A Time.timeScale script API (slow motion)
- [x] 335. Default gravity values per project
- [x] 336. Default audio settings
- [x] 337. Localization: language files (uz, ru, en)
- [x] 338. A localization script API (Localize("key"))
- [x] 339. Choosing the editor language (Uzbek/English)
- [x] 340. Project settings documentation

## M18 Runtime player

- [x] 341. Reading the player window settings from the project
- [x] 342. Toggling fullscreen with F11
- [x] 343. An FPS limit
- [x] 344. A splash screen (the Zamin logo)
- [x] 345. The player command line: --windowed, --resolution
- [x] 346. The player writes a log file
- [x] 347. A message window on a crash
- [x] 348. An API to hide/lock the cursor in the player
- [x] 349. Adapting to a change of the screen size
- [x] 350. Leaving unused assets out of the export
- [x] 351. Optimizing the script module in the export (Release)
- [x] 352. Export: a .desktop file and an icon
- [x] 353. Showing an export report (size, files)
- [x] 354. Pausing in the player (when the focus is lost)
- [x] 355. A gamma and brightness setting in the player
- [x] 356. A player run test (headless)
- [x] 357. An export test (automatic)
- [x] 358. A sample of a minimal game menu for the player
- [x] 359. Player version information
- [x] 360. Runtime documentation

## M19 Math and core

- [x] 361. Perlin/Simplex noise functions
- [x] 362. An AnimationCurve type (keys, evaluation)
- [x] 363. Spline paths (Catmull-Rom) and a component
- [x] 364. A script for moving along a spline
- [x] 365. Color utilities (HSV, lerp, hex)
- [x] 366. Math utilities (Remap, SmoothDamp, MoveTowards, Approximately)
- [x] 367. Quaternion helpers (LookRotation, Slerp)
- [x] 368. Job system / thread pool
- [x] 369. Parallel for (for particles and animation)
- [x] 370. Memory statistics in the profiler
- [x] 371. A log file and a log level setting
- [x] 372. Better asserts (with a message)
- [x] 373. Timer and Stopwatch utilities
- [x] 374. String utilities (split, trim, format)
- [x] 375. A stronger UUID generation test
- [x] 376. File system helpers (path normalization)
- [x] 377. Bounding volume helpers (AABB, sphere, ray intersection)
- [x] 378. Geometry tests (ray-AABB, ray-sphere)
- [x] 379. Math tests
- [x] 380. Core documentation

## M20 Navigation and AI

- [x] 381. Grid based A* pathfinding (2D)
- [x] 382. Building the pathfinding grid from a tilemap
- [x] 383. NavGrid 3D (along the ground plane, detecting obstacles)
- [x] 384. A NavAgent component (walking along a path)
- [x] 385. Path smoothing (string pulling)
- [x] 386. Dynamic obstacles (updating the grid)
- [x] 387. Steering: seek, flee, arrive, wander
- [x] 388. Obstacle avoidance (with raycasts)
- [x] 389. An AI state machine helper class
- [x] 390. A line of sight check
- [x] 391. A patrol path (waypoints) component
- [x] 392. Navigation debug drawing
- [x] 393. Navigation tests
- [x] 394. An AI sample: an enemy chases (2D)
- [x] 395. An AI sample: a 3D NPC on patrol
- [x] 396. Formation/group movement (simple)
- [x] 397. A behavior tree (minimal) helper
- [x] 398. A navigation performance test
- [x] 399. Navigation serialization
- [x] 400. AI documentation

## M21 Prefabs and serialization II

- [x] 401. Prefab instances are linked to the original prefab (link)
- [x] 402. Applying prefab changes to the instances (Apply)
- [x] 403. Returning an instance to its original state (Revert)
- [x] 404. Showing prefab overrides in the inspector
- [x] 405. Nested prefabs
- [x] 406. A prefab editing mode (opening it on its own)
- [x] 407. Remapping EntityRefs in prefab instances
- [x] 408. Remapping EntityRefs internally on duplication
- [x] 409. Scene file version and migration
- [x] 410. Recovering a broken scene file (partial loading)
- [x] 411. A serialization fuzz test (random JSON)
- [x] 412. A serialization benchmark of large scenes
- [x] 413. Prefab variants (inheriting from a base prefab)
- [x] 414. Prefab thumbnails
- [x] 415. Dragging a prefab from the content browser into the scene many times
- [x] 416. Hot reload of prefab changes
- [x] 417. Prefab serialization tests
- [x] 418. Merging the clipboard format with the prefab one
- [x] 419. Checking prefabs in the scene export
- [x] 420. Prefab documentation

## M22 Quality and tests

- [x] 421. A build and tests with AddressSanitizer
- [x] 422. Tests with UndefinedBehaviorSanitizer
- [x] 423. Checking for memory leaks (LeakSanitizer)
- [x] 424. Cleaning up warnings (-Wall -Wextra -Wpedantic)
- [x] 425. Headless screenshot tests for the renderer in CI
- [x] 426. An editor smoke test (every panel opens)
- [x] 427. An automatic test of script hot reload
- [x] 428. An automatic export test
- [x] 429. A physics determinism test
- [x] 430. A performance benchmark (10k sprites, 1k meshes)
- [x] 431. An ECS benchmark
- [x] 432. A serialization benchmark
- [x] 433. Test framework: a filter by test names
- [x] 434. Test framework: a timing report
- [x] 435. A code coverage report (gcov)
- [x] 436. A clang-format configuration
- [x] 437. CI: a sanitizer job
- [x] 438. CI: artifacts (the player build)
- [x] 439. A document with the list of regression tests
- [x] 440. A quality report

## M23 Documentation

- [x] 441. docs/Scripting.md - a guide to C++ scripts
- [x] 442. docs/Components.md - a reference of every component
- [x] 443. docs/Editor.md - an editor guide
- [x] 444. docs/Tutorial-2D.md - a first 2D game
- [x] 445. docs/Tutorial-3D.md - a first 3D game
- [x] 446. docs/Physics.md
- [x] 447. docs/Rendering.md
- [x] 448. docs/Animation.md
- [x] 449. docs/UI.md
- [x] 450. docs/Audio.md
- [x] 451. docs/Architecture.md - the internal architecture
- [x] 452. docs/BuildAndExport.md
- [x] 453. A Doxygen configuration
- [x] 454. Filling in the API comments in the headers
- [x] 455. FAQ and troubleshooting
- [x] 456. A contribution guide (CONTRIBUTING)
- [x] 457. CHANGELOG
- [x] 458. Illustrated guides (screenshots)
- [x] 459. Updating the README
- [x] 460. A documentation index

## M24 Sample games

- [x] 461. Top-down shooter (2D): the player, shooting, enemies
- [x] 462. Top-down shooter: waves and the score
- [x] 463. Breakout (2D): the paddle, the ball, the bricks
- [x] 464. Breakout: bonuses and levels
- [x] 465. A 3D FPS controller sample (mouse look)
- [x] 466. FPS: targets and shooting (raycast)
- [x] 467. A puzzle game (match-3 or sokoban)
- [x] 468. Endless runner (3D)
- [x] 469. Racing (simple car physics)
- [x] 470. Showing the samples in the launcher (with a picture)
- [x] 471. A README for every sample
- [x] 472. Headless tests for the samples
- [x] 473. Sample assets (procedural)
- [x] 474. Sounds in the samples (procedural wav)
- [x] 475. UI menus in the samples
- [x] 476. SaveData in the samples (the record)
- [x] 477. Input actions in the samples
- [x] 478. Exporting and checking the samples
- [x] 479. A gallery of the samples (docs)
- [x] 480. A performance check of the samples

## M25 Optimization and polish

- [x] 481. Renderer2D batching optimization (fewer flushes)
- [x] 482. Fewer per-frame allocations
- [x] 483. World transform caching (dirty flag)
- [x] 484. Caching ECS views
- [x] 485. Font atlas caching and kerning
- [x] 486. Texture loading on a background thread (async)
- [x] 487. Model loading on a background thread
- [x] 488. Shader caching (program binary)
- [x] 489. Moving uniforms to UBOs
- [x] 490. Culling in the shadow pass
- [x] 491. Parallelizing the particle simulation
- [x] 492. Parallelizing the animation evaluation
- [x] 493. Editor UI performance (large hierarchies - clipper)
- [x] 494. Large folders in the content browser (clipper)
- [x] 495. A shorter startup time
- [x] 496. Less memory use
- [x] 497. A 60 FPS check of every sample
- [x] 498. A bottleneck report from the profiler
- [x] 499. The final polish: UI texts and translations
- [x] 500. The final regression and all the tests
