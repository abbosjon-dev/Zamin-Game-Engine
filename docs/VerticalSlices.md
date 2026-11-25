# Vertical slices

Two small finished games built only from the engine's gameplay packages and played end to end by a deterministic
demo pilot for acceptance: the 2D *Chiroqbon* (`Projects/LanternKeeper`) and the 3D *Dovon mash'ali*
(`Projects/BeaconPass`). Both pass the package's stress, UI input/scale, fresh-process resume, relocated export and
profiling gates ([Gates](#gates), [Resume, export and profiling](#resume-export-and-profiling); evidence in
docs/TASKS.md).

## Chiroqbon (2D, `Projects/LanternKeeper`)

A compiler-free top-down action-adventure (`ScriptModulePath ""`): title menu, two levels, victory and defeat screens.
Soya Bek stole the Lamps of Dawn; Nilufar wins one back from the bandits of the orchards (**Boglar**) and two from the
crypt below the fortress (**Yertola**), defeats Soya Bek and lights the Dawn Lamp on the altar.

| Scene | Content |
|---|---|
| `Menu` | Lit diorama, *Yangi o'yin* / *Davom etish* (only for an unfinished run) / *Sozlamalar* (music and SFX volume, fullscreen, uz/en) / *Mualliflar* / *Chiqish*, best time |
| `Boglar` (46 x 26) | Herbs, coins, the sword chest, a bandit camp with an archer, a shrine, the chief's yard (chief + bandits + archer), the gate opened with the chief's key, the first lamp, the crypt stairs |
| `Yertola` (44 x 30) | Crypt hall with skeletons, two side rooms (lamps 2 and 3, the armour chest, archers), a shrine, the boss arena (Soya Bek, phase 2 below 55 % wakes two spirits and a shadow ring), the altar |

Systems in one flow: `Gameplay.Motor2D` (top-down) and the 2D follow camera; melee (`Gameplay.Attack`) and abilities
(Bolt projectile with Burn, Nova area with Shock unlocked at level 2, Mend regeneration at level 3, potions); AI
brains for bandits, the chief and skeletons (built-in strikes) and for archers, spirits and the boss with delegated
attacks (`Strike=false`, the graph casts Arrow/Spit/Orb); a five-quest campaign book (850 XP, level 4) with automatic
claims; equipment (Qilich, Tumor, Zirh) and an inventory page with *Kiyish*; HUD kinds 0/2/7/8/9, the quest tracker
and the combat feed; pause, bag, level card, victory and defeat panels; directional sprite locomotion through
model-free Animators with `DriveSpriteClips`; 2D lights, particles, music, ambience and sound effects.

### Graphs and identities

| Graph | Host | Role |
|---|---|---|
| `Hero.zgraph` | the hero (UUID 1000) in both levels | the whole campaign configuration (identical in both levels: transfer signature), input and the pilot writing the same intent variables, motor/strikes/abilities, interaction, animation, deaths/respawn, shrine saves, unlocks, claims |
| `Game.zgraph` | the controller (1100) in both levels | panels and HUD bindings, interactables (from override tables), entry (new / carry / continue), level completion with the transfer, boss music/phases, victory/defeat, run time |
| `Enemy.zgraph` | every enemy (instance overrides pick the archetype) | health/armour/AI configuration, delegated ranged attacks, boss strikes and rings, animation, hit sparks, kill loot straight into the bag |
| `Menu.zgraph` | the menu controller | panels, settings, demo auto-start, new game / continue |

Shared UI and controllers have identical UUIDs in every scene (1000-1099 hero/camera, 1100-1199 controllers, 2000-4999
geometry and props, 5000-5499 enemies, 5500-5999 interactables, 6000-7999 UI, 8000-8999 effects). Save keys:
`Hero`, `L1-Bandit-01`, `L2-Boss`, `L1-Coin-03`, ...; schema `Chiroqbon-1`.

### Run flow and saves

| Event | Writes |
|---|---|
| New game | GameState `LK.Run.*` reset, `Entry=new` → Boglar → autosave `Run1` |
| Shrine | full heal, then `SaveWrite(Run<n>)` (v4: combat is present), checkpoint, `LK.Run.Save/Step/Time` |
| Level 1 exit (Gate claimed, hero alive) | level card (pauses the world, timed in real time) → `TransferWrite(Carry)` (retried after 1 s on failure) → `Run.Level=2, Run.Save=0, Entry=carry` → Yertola: `TransferLoad`, heal, autosave `Run2` |
| Continue | the level's own save when `Run.Save == Run.Level`, else the transfer (level 2), else a fresh start |
| Direct start (Editor Play or `StartScene` on a level, `Entry` unset = `new`) | a fresh run of that level: level 2 never reads a stale `Carry.json` |
| Defeat (3 deaths since the load) | defeat screen → *Oxirgi saqlashdan* reloads the level from its save |
| Victory (Boss and Dawn claimed) | `Run.Finished=1`, best time; the menu no longer offers Continue |

Kill loot goes straight into the bag on the enemy's own `Died` event (a restore clears events, so a loaded death
never pays again); pickups, chests and the gate are one-use interactions bound to save keys; claimed quests never
grant again on a load or a transfer.

### Demo pilot and markers

`ZAMIN_DEMO=1 ./build/bin/ZaminPlayer Projects/LanternKeeper` (or the `Pilot` / `Demo` overrides) lets the pilot play:
it follows routes the generator compiled with A* over each level grid (waypoint codes: go, interact, hold until
clear, equip, finish), fights what `Gameplay.Sense` sees (Bolt, melee under 1.4 m, Nova under 2.6 m, potion and Mend
under 45 HP) through the same intent variables the input writes, and reports `SLICE_BOT_STUCK` after 8 s without
progress. Plans: 0 win, 1 lose first (no fighting until a defeat), 3 stop after the level 2 shrine save.
Log contract: `Menyu: ...`, `SLICE_LEVEL_START`, `SLICE_RESUMED`, `SLICE_SAVED`, `SLICE_LEVEL_COMPLETE`, `SLICE_VICTORY`,
`SLICE_DEFEAT`, `SLICE_TRACE`, `SLICE_CONFIG_FAILED`, `SLICE_IO_FAILED`, `SLICE_BOT_STUCK`.

### Generation and tests

`python3 Tools/samples/gen_lantern_keeper.py` writes everything deterministically (two runs give identical files):
painted characters with 4-direction walk cycles (`slicekit.py`), tilesets, props, UI frames and icons, music and
sounds, localization, scenes and graphs (`lantern_graphs.py` on `graphkit.py`), and `Tests/oracle.json` (test data,
kept outside `Assets/` so it never ships). Headless tests (`ZaminTests --filter LanternKeeper*`): graphs validate and scenes load; the oracle
plays menu → Boglar → transfer → Yertola → victory (850 XP, level 4, 5/5 quests, all coins, lamps consumed, three
items equipped, Nova/Mend unlocked, boss dead, no projectile in flight, transfer v1 and save v4 files); the lose plan
reaches the defeat screen, reloads and completes level 1 with exactly the normal progress; two fresh level 1 runs
give identical traces at identical frames.

Player/Editor acceptance (`python3 Tests/slice_workflow_test.py [--build build] [--out DIR] [--phases ...]`, CI step
*Chiroqbon 2D vertical slice*): private project copies, one HOME per run, fixed 30 fps capture, software GL. P0 a
keyboard tape (menu → settings: music slider and *Orqaga* → new game → move, Bolt, strike, bag, pause); P1 the demo
pilot writes level 1, the transfer and the level 2 shrine save (plan 3); P2 a fresh process continues from that save,
beats the boss and reaches the victory screen (850 XP, level 4, 5/5 quests); P3 a fresh process: no Continue after the
victory; P4 the lose plan (three deaths → defeat → reload → level 1 complete with 200 XP / 13 coins); E0 Editor Play of
Boglar; E1 Editor Play of Yertola next to P1's stale `Carry.json` (a fresh level 2 run). Every phase rejects GL
errors, graph errors, `SLICE_*_FAILED` and a stuck pilot, checks the last frame with `png_check.py` and the transfer
and save JSON; `<out>/Report.json` lists the phases and their times.

## Dovon mash'ali (3D, `Projects/BeaconPass`)

A compiler-free third-person action-adventure in one level. The Black Warden put out the signal fire of the mountain
pass; the courier Temur climbs from the valley camp through the caravanserai to the watchtower plateau, gathers three
fire stones, defeats the Warden and relights the beacon before the caravan arrives.

| Scene | Content |
|---|---|
| `Menu` | The pass at golden hour seen from above the camp (a slow sway), *Yangi o'yin* / *Davom etish* (only for an unfinished run) / *Sozlamalar* / *Mualliflar* / *Chiqish*, best time, input hints |
| `Dovon` | 120 x 120 m terrain (four layers) with packed pines and rocks on the slopes and snow peaks around; the camp (tents, fire, the sword chest, a potion), a jump onto a ledge (fire stone 1), two gate guards and the caravanserai gate, the courtyard (a guard, an archer on the wall walk, fire stone 2, a shrine), the stable (a guard, the shield chest, fire stone 3), the plateau shrine, the Warden and an archer at the tower, the beacon platform with its stairs |

Systems in one flow: a `CharacterController3D` driven by `Gameplay.Motor3D` relative to the camera (run 5.4 m/s, walk
1.8 m/s while *Walk* is held, jump with coyote time and buffering), the body turned to its heading with `Atan2` +
`LerpAngle`; the orbit camera (`Camera3D.zgraph`: `Gameplay.Camera` with mouse / right stick look and a sphere cast
against World|Wall, the pilot's camera turning behind the heading, a pull-back reveal at the beacon); melee on the
Attack clip's `Hit` event (`Gameplay.Attack` along the facing), Bolt on the Cast clip's `Release` event (projectile +
Burn), Zilzila (ground slam area + Shock, unlocked at level 2), potions; guards with delegated strikes
(`Strike=false`: the brain chases and enters Attack, the graph plays the clip and its `Hit` event calls
`Gameplay.Strike`), archers casting Arrow, the Warden with a telegraphed shockwave (`Gameplay.AreaApply`) and enrage; a
four-quest book (*Dovon yo'li* — gate guards and the gate, *Olov toshlari*, *Qora qorovul*, *Mash'al*: 800 XP, level
4); equipment (Shamshir +10 attack, Qalqon +30 armour) shown on the body through bone attachments; HUD kinds 0/7/9
(health, energy, level), item counters (kind 2), the ability bar with cooldown fills (kind 8), the quest tracker and the
Warden's bar; pause, bag, victory and defeat panels;
golden-hour sun with shadows, fog, braziers with point lights and fire, the beacon's fire column; music, ambience,
footsteps and sound effects.

### Humanoids and locomotion

`Tools/samples/gltfkit.py` writes skinned glTF humanoids (one 21-joint skeleton for the hero, the guards and the
Warden, 1–2 joint weights, materials skin/cloth/leather/metal) with procedurally sampled clips: Idle, Walk (1.11 s for
a 2.0 m stride), Run (0.72 s for a 3.9 m stride), Fall, Land, Attack, Cast, Hurt, Death. The Animator controller:

| Part | Content |
|---|---|
| Parameters | `Speed`, `Grounded`, `VerticalSpeed` (from `Gameplay.BindAnimator`, damped 0.1 s), `Dead` |
| States | `Locomotion` (BlendTree1D on Speed: Idle 0, Walk 1.8, Run 5.4 — the thresholds are the gait speeds, so a pure Walk or Run plays at exactly its stride), `Air` (not grounded and rising faster than 1.5 m/s or falling faster than 3 m/s), `Land` (exit time 0.85), `Dead` (from any state) |
| Layers | `UpperBody` (Override from the Spine: Attack / Cast through `Animator.PlayLayer` with `Restart`, weight faded in and out by the graph), `Hit` (Additive Hurt on every damage event) |
| Events | `Hit` at 0.42 of Attack, `Release` at 0.45 of Cast, `Footstep` at 0 and 0.5 of Walk and Run (a `Footsteps` component plays four surface sounds scaled by speed) |

`Animator.PlayLayer`'s `Restart` pin (default off; graphs saved before it keep their behaviour) starts a layer's clip
over when it is already playing it, so a second swing of the same attack replays its `Hit` event.

### Graphs, run flow and saves

| Graph | Host | Role |
|---|---|---|
| `Hero.zgraph` | the courier (UUID 1000) | campaign configuration, input and the pilot writing the same intent variables, motor and heading, upper-body clips and their events, interaction, rule events (deaths, shrine saves, pickups, hurt), respawn, unlocks and claims |
| `Camera3D.zgraph` | the camera (1003) | the orbit camera |
| `Game.zgraph` | the controller (1100) | panels and HUD, interactables, entry (new / continue), quest starts, Warden music and phases, victory and defeat, run time |
| `Enemy.zgraph` | every enemy (instance overrides pick guard / archer / Warden) | health, armour and AI configuration, clips and delegated strikes/casts, the Warden's shockwave, hit feedback |
| `Menu.zgraph` | the menu controller | camera sway, panels, settings, demo auto-start, new game / continue |

One level, no transfer: *Yangi o'yin* resets `BP.Run.*` and autosaves `Run1` on entry; *Davom etish* loads `Run1`;
shrines (the courtyard and the plateau) write `Run1` (v4) with a checkpoint and a full heal; three deaths since the
last load end on the defeat screen, whose *Oxirgi saqlashdan* reloads the level; the victory sets `Run.Finished` and
the best time. Schema `Dovon-1`; save keys `Hero`, `Dovon-Guard-01`…, `Dovon-Archer-01/02`, `Dovon-Warden`, the chests,
stones, potions, gate, shrines and beacon.

A shrine is a rest stop: the pilot comes to a standstill before it uses one, and the hero polls its rule events in
`LateUpdate` right after the interaction, so the save holds the state at the end of that frame — the phase in which
`Game.zgraph` restores a save on entry — and a resumed run starts from exactly the saved state. A restore stops every
body and navigation agent (a save keeps no velocities or paths), so enemies that were walking start again from a
standstill: a resumed run reaches the same events in the same order with the same progress, on frames that may
differ. The pilot's route position (`BP.Run.Step`) and the body's heading (from the restored rotation) are taken up
on the `Resumed` message.

### Tests

`python3 Tools/samples/gen_beacon_pass.py` writes everything deterministically (two runs give identical files; assets
5.96 MiB, the largest scene 547 KB) and `Tests/oracle.json` (test data outside `Assets/`). Headless tests (`ZaminTests --filter BeaconPass*`, models read on the CPU so the real
Animator runs): graphs validate and scenes load (controller shape, blend thresholds, clip lengths = stride / speed);
the oracle plays menu → Dovon → victory (800 XP, level 4, 4/4 quests, three stones consumed, sword and shield
equipped, Zilzila unlocked, Warden dead, no projectile in flight, save v4); the locomotion run records pure Walk and
Run weights, Air and Land, the UpperBody layer at weight 1 during strikes, Hurt on damage, footstep spacing at half the
stride (±10 %), the damped Speed parameter against the measured speed (≤ 0.05 m/s), the feet never below the terrain
and the camera never inside a wall; the lose plan dies three times (Dead state), reloads and claims the gate quest
once; the save/resume test compares the courtyard save with a fresh process's restore (hero, enemies, used objects,
bag, quests, loadout) and both continuations to the plateau shrine, then quick-saves the Warden fight with a Bolt in
flight and checks the restored projectile and its flight frame by frame; two fresh runs give identical traces at
identical frames.

Player/Editor acceptance (`python3 Tests/slice_workflow_test.py --game BeaconPass`, CI step *Dovon mash'ali 3D vertical
slice*; 640x360, quality Low): P0 a keyboard tape (menu → settings → new game → run, jump, strike, Bolt, bag, pause →
*Inventar*); P1 the demo pilot plays to the courtyard shrine save and stops (plan 3: save v4, 100 XP, two stones); P2 a
fresh process continues from it to the victory screen (800 XP, level 4, 4/4 quests); P3 no Continue after the
victory; P4 the lose plan (three deaths → defeat → reload → the gate quest once); E0 Editor Play of Dovon.

## Gates

The stress and UI gates of both slices (P5 item 3). Every gate fails on a broken invariant, not only on a slow number.

### Animation and physics stress

Headless, in `ZaminBench` (Release numbers, budgets and invariants in
[Benchmarks.md](Benchmarks.md#vertical-slice-stress-darvozalari-fizika-animatsiya-oyin-ichida---physics---animation---slice-stress);
baselines `Tests/Baselines/{physics,animation,slicestress}-release.json`, `Tests/bench.sh --only physics,animation,slicestress
--compare`; `ctest -L bench` runs each small with its invariants):

| Gate | Workload | Budget | Release, 5 runs (mean / p95) |
|---|---|---|---|
| P1 `--physics --2d` | 60×40 m tilemap arena, 2 048 bodies, 64 `StepMotor` actors, 128 projectiles | step ≤ 4 / ≤ 6 ms | 1.56–2.15 / 2.3–3.1 ms |
| P2 `--physics --3d` | pit, ramps, 0.25 m stairs, 35° slope, 65×65 terrain, 512 bodies, 64 Character Controller 3D, 128 sphere casts | step + CC ≤ 8 / ≤ 12 ms | 1.51–1.80 / 2.00–2.17 ms |
| A1 `--animation` humanoids | 256 × `Hero.glb` (21 joints) with Dovon's controller, half off-screen | jobs 3 ≤ 1.5 / ≤ 2.0 ms, jobs 0 ≤ 4.0 ms, 0 allocations | 0.57–0.82 / 0.66–1.10 ms; jobs 0 0.99–1.73 ms; 0.0 allocations |
| A2 `--animation` sprites | 1 024 model-free animators, `DriveSpriteClips`, 2D blend directions turning | ≤ 0.6 ms, 0 allocations | update 0.30–0.48 ms; 0.0 allocations |
| P3 `--slice-stress --2d` | Boglar: 32 brains (the scene limit), 200 crates, 64 projectiles, 256 sprite animators, 48 Light 2D | game code ≤ 6 / ≤ 10 ms | 2.80–3.42 / 3.00–4.71 ms |
| A3 `--slice-stress --3d` | Dovon: 24 animated guards + the level's enemies (27 brains), hero, 64 projectiles, 200 Jolt crates | game code ≤ 8 / ≤ 12 ms | 3.89–5.45 / 5.41–6.37 ms |

Invariants, all held: no body out of the arena / pit, no NaN, 2D speed ≤ 6.9 m/s; Character Controller feet never more
than 0.0002 m below the ground, at least 90.7 % of the commanded distance walked; two simulations give the same hash;
blend weights sum to 1 within 3e-8, the pose hash is identical with 0 and 3 workers, culled animators are not posed;
animation events once per crossing at 20/30/60/144 Hz (per animator equal, ±1 at the window's end); sprite frame =
StartFrame + floor(phase × count) exactly and the phase continuous across direction switches; no graph error, every
brain alive or dead as its health says. The sprite bench found a real bug: a direction switch fired the current
frame's event a second time (the new clip restarted at the same frame); the driven clip now keeps its step across a
switch (`SpriteAnimationComponent::DrivenState`, [Animation.md](Animation.md)). The animator update no longer
allocates per frame on one thread (it made 223 allocations a frame for 24 humanoids, 870 for 64 sprites).

The in-game stress (A3, P3) runs headless on private copies of the real levels with their real graphs instead of
separate `Stress` scenes in the Player: the slices' assets are at their 6 MiB budget, and llvmpipe's drawing would bury
the game code's time.

### Localization, settings and tiles

Every player-facing string in both slices (prompts, banners, HUD and stats labels, level titles, item, quest, objective
and combat feed names) goes through a localization key with its Uzbek fallback: graph texts use `Localization.Text`,
the bag, the quest log and the combat feed look up `item.<Key>`, `quest.<Quest>[.<Objective>]` and `combat.<Key>`, and
the generators write `uz.json` / `en.json` with the same keys (checked by `Tests/samples_assets.py`). Both games start
in English (`"DefaultLanguage": "en"`, languages `en`, `uz` in the project file); Uzbek is one press of the settings
menu's language button away and is kept with the player's settings. Prompts carry
`{action:...}` tokens; the UI audit's `RawHint` rule fails any visible text that still shows one unexpanded.
Settings are saved whenever the settings panel closes, by *Back* (*Orqaga*) or by Back/Escape (`SLICE_SETTINGS` then
`Menyu: Asosiy`); *Fullscreen* (*To'liq ekran*) and the language go through `Display.SaveSettings` and
`Localization.SaveSettings`, so a fresh process starts with the same choice.
Chiroqbon's ground uses a 47-variant blob autotile for paths (no holes in 2x2 corners), a 4-neighbour water autotile,
seamless grass and a decal layer (`Bezak`: petals, clover, pebbles, moss, cracks, cobwebs, wall shadows) between the
ground and the walls. Dovon's terrain samples steep slopes triplanar, so cliffs no longer stretch.
The long runs (oracles, lose plans, resume matrices, determinism) are `SLOW_TEST`s: `ZaminTests --quick` (ctest label
`unit`) skips them and `ZaminSliceTests` (`--slow`, labels `slices;slow`) runs them.

### UI input and scale

**UI scale.** Both settings screens have an *Interfeys o'lchami* slider (80–120 %, steps of 10 %) that calls
`Display.SetUIScale` (`Scene::SetUIScaleMultiplier`, multiplying every canvas' scale), is logged as
`SLICE_SETTINGS ... ui=1.20` and saved in `<Game>.Settings.UIScale`; the game applies it at start, so a fresh process
comes back at the chosen size.

**Audit.** `Scene::AuditUI` lays the UI out for any view size without drawing and `CheckUIAudit` names each defect
(`Margin`, `Offscreen`, `Overlap`, `TextOverflow`, `MinFont` - 12 px at 800×600, 14 px from 1024×768, proportionally
less below 100 % UI scale - `Focus`, `Unreachable`, `SideBand`; [UI.md](UI.md)). The canvases stay *Match width or
height 0.5* at reference 1920×1080; the layouts were fixed instead (texts ≥ 25 units, the controls line stretched to
78 % of the view and wrapping, the 2D menu's map widened so a 21:9 view never sees past it).

- **Headless** (`ZaminTests --filter SliceMenuLayout*`): every scene of the slice played for 1.5 s, its first screen
  (main menu / HUD) and then each of its gameplay UI panels opened in turn (Chiroqbon: Menu, Boglar, Yertola - 15
  screens; Dovon mash'ali: Menu, Dovon - 8), audited at 800×600, 1024×768, 1280×720, 1600×900, 1920×1080 and
  2560×1080, each at UI scale 80 / 100 / 120 %: 270 + 144 audits, 13 932 + 5 472 elements, 0 issues, smallest text
  9.6 px (80 % at 800×600). `SliceMenuLayoutCatchesABrokenLinkAndTinyText` breaks the music
  slider's links (→ `Unreachable`) and shrinks the version line (→ `MinFont`).
- **Player** (`python3 Tests/slice_ui_test.py`, CI step *Slice UI input/scale gate*; `ZAMIN_UI_AUDIT` audits each screen
  at the six sizes whenever the scene or its open panel changes):
  - U1 keyboard tour (menu → settings: music, UI scale to 120 % → credits → new game → HUD → pause → bag → back):
    60 audits per game over 6 screens, 0 issues, smallest text 12.03 px.
  - U2 a fresh process with the same HOME starts at 120 %, then the gamepad tour (D-pad, A, B, Start) sets 80 %
    (`ui=0.80`, *Kiritish qurilmasi: gamepad*): 60 audits, 0 issues; a mouse run clicks *Sozlamalar* and *Orqaga*
    where the audit says they are.
  - U3 seeded fuzz, 2 000 frames of random navigation / submit / back / pause / inventory on the keyboard and on the
    gamepad per game: 534 / 504 (Chiroqbon) and 546 / 450 (Dovon mash'ali) audits, at most 2 panels open, 0 invariant
    breaks (time scale back to 1 with no panel open), 0 errors.
  - UW the title menu in a real 2560×1080 window: 6 audits, 0 issues, screenshot.
- **Mutations** (in the Player gate): clearing the music slider's Up/Down links fails U1 (the tour cannot reach
  *Orqaga*, and the audit reports `Unreachable` for the three widgets below it); a 25 → 18 unit version line fails U1
  with 12 `MinFont` issues (13.3 px < 14 at 1024×768).

### Resume, export and profiling

The fresh-process, export, editor and profiling gates of both slices (P5 item 4).

**Resume matrix (headless,** `ZaminTests --filter *ResumeMatrixHeadless`**).** One normal run keeps the files a process
killed at each step would leave behind; a fresh process (`SaveData::Reload`, `GameState::Load`, the demo menu's
Continue) on each copy must resume a consistent run, and no reward is granted twice:

| Cut / damage | Chiroqbon | Dovon mash'ali |
|---|---|---|
| after New game, before the autosave | - | Continue offered, fresh start at the camp, autosave written then |
| after a save, before its GameState | level 1 exit: the transfer written, `Run.Level` still 1 → level 1 from its shrine save; the exit replays and writes the same transfer (200 XP, same bag) | shrine save written, `Run.Time` old → the shrine save (100 XP, two stones, the saved pose to < 1 mm) |
| after GameState, before the next level's autosave | `Run.Level=2, Run.Save=0` → level 2 through the transfer (200 XP, 13 coins, full health) | - |
| after the autosave, before its GameState | level 2 through the transfer again, the autosave rewritten | - |
| a changed byte in the primary save (checksum) | its backup - the level 2 autosave - with *Zaxira nusxadan tiklandi* (`recovered=1`) | its backup - the camp autosave - with the same banner |
| primary and backup torn | `SLICE_IO_FAILED load`, *Saqlash o'qilmadi - daraja boshidan*, level 2 again from the transfer; level 1 starts fresh; with the transfer also torn a fresh level 2 | `SLICE_IO_FAILED load`, the same banner, a fresh start; the autosave is valid again |

The matrix found a real bug: level 2's fallback after an unreadable save did the transfer load and the autosave in the
same frame as the failed load - three save file operations, one over the scene's budget of two - so the autosave
failed (`SLICE_IO_FAILED autosave`). The fallback now continues one frame later. Mutation: without Continue's
`Run.Save == Run.Level` guard the matrix fails three times.

**Player/Editor** (`python3 Tests/slice_workflow_test.py --game LanternKeeper|BeaconPass --exports`, CI steps
*Chiroqbon 2D / Dovon mash'ali 3D vertical slice ...*), besides P0-P4, E0/E1:

- **C** damaged saves on copies of P1's files, each continued by a fresh Player: a changed byte → the backup with the
  *recovered* banner; save and backup torn → `SLICE_IO_FAILED load`, the visible message, the level's entry (2D: the
  transfer, 200 XP; 3D: a fresh start, 0 XP) and a valid save again. A screenshot 24 frames after the level start
  shows the banner.
- **P6** gamepad tape: the D-pad moves the music slider (`music=0.85`), A starts a new game, the left stick walks the
  hero, RB/X fire and strike, Back/B open and close the bag, Start pauses and *Saqlab, bosh menyuga* writes the quit
  save (`SLICE_SAVED lvl=1 at=quit`, v4) and the menu offers Continue.
- **E2** Editor Play of the menu (demo): New game and the scene switch into the level inside Play mode.
- **X** the shipped game is exported (`Tests/export_test.sh`: version, a timed run, a screenshot run, relocation);
  its launcher with `ZAMIN_DEMO=1` plays the attract run and is killed by `ZAMIN_MAX_FRAMES` between two saves (2D:
  after the level 2 carry autosave, 3D: after the courtyard shrine save); the folder is moved for real (`mv`); from the
  new place a fresh process continues the save to the victory screen (`ZAMIN_SCREENSHOT_ON="SLICE_VICTORY|120|...|quit"`),
  a third shows no Continue, a fourth starts a new game (0 XP) and the best time stays.
- **D** determinism: P1's trace in the Player (960x540 / 640x360, fixed 30 fps game clock) against the headless one
  (`ZAMIN_SLICE_TRACE_OUT` of `*Determinism`): Chiroqbon's level 1 is the same 23 events in the same order, Dovon
  mash'ali's courtyard run the same first 12 of the headless 26.

**Editor** (`xvfb-run -a -s "-screen 0 1600x1000x24" python3 Tests/slice_editor_test.py`, CI step *Slice Blueprint
authoring ...*): in the real Blueprint window `Gameplay.TransferWrite`, `Gameplay.TransferLoad` and `Gameplay.Steer` are
added, saved and undone; LoadScene's `FadeOut` is typed to 0.6, UIBindHUD's Kind combo scrolled to Kind 8
*Qobiliyat* and AIConfigure's `Strike` checkbox turned off - each saved, undone and redone. Then
`ZAMIN_EDITOR_GRAPH_QA` opens every slice graph in the window (Chiroqbon: Game 625/737 nodes/links, Enemy 174/205,
Menu 160/175, Hero 560/678; Dovon mash'ali: Camera3D 38/46, Game 490/583, Enemy 180/211, Menu 168/183, Hero 655/787),
each with 0 compile diagnostics and no log error. `Tests/editor_smoke.sh Projects/LanternKeeper Projects/BeaconPass`
(every editor window, page and popup over the slices; CI step) passes 243/243 and 268/268 steps with 0 errors.

**Profiling** (`python3 Tests/slice_bench.py --build build-release --check --compare Tests/Baselines/slices-release.json`;
the table and its method in [Benchmarks.md](Benchmarks.md#vertical-slicelar-playerda-profiling-va-regressiya-darvozasi-testsslice_benchpy)):
every slice scene in the Release Player's `--benchmark` (the levels with the demo pilot), at 284x160 Low and 1280x720.
The Player's benchmark report now carries `GameCode` - the update layer without its drawing, per measured frame - so
the game's own CPU work is gated apart from llvmpipe's rasterizer. Small window (best of 3 runs): game code 0.32 /
2.02 / 2.03 ms (Chiroqbon Menu / Boglar / Yertola) and 0.53 / 3.50 ms (Dovon mash'ali Menu / Dovon), p95 at most 4.40
ms (gates ≤ 3 ms 2D, ≤ 4 ms 3D, p95 ≤ 6 ms); frames 3.5-5.7 ms in 2D (≤ 16.6 ms) and 22-28 ms in 3D (≤ 33.3 ms: the
software rasterizer draws the level's ~74 500 triangles twice, shadow map and main pass); the slowest frame of a warm
session ≤ 51 ms (a first session pays llvmpipe's shader JIT once: 1.9 s in Yertola's first fight, 345 ms in Dovon).

**Soak** (`python3 Tests/slice_bench.py --build build-release --no-bench --soak 18000`): the attract loop (`ZAMIN_DEMO=1`,
284x160 Low) for 18 000 frames - 10 game minutes - per game, sampled every 30 frames by `ZAMIN_SOAK_STATS`. Chiroqbon
won 3 times and came back to the menu 3 times, each time with 57 entities, 302.9 MiB resident and 99.2 MiB of engine
memory (growth after the first loop 0.0 MiB); Dovon mash'ali won 7 times, 278 menu entities each time, the resident
memory 269.4 → 275.5 → 278.8 MiB over the first three returns and then flat (278.9 MiB; growth 9.5 MiB ≤ 32), the
engine's own memory 39.0 MiB at every return. No log error in either.

