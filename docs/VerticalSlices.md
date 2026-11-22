# Vertical slices

Two small finished games built only from the engine's gameplay packages and played end to end by a deterministic
demo pilot for acceptance: the 2D *Chiroqbon* (`Projects/LanternKeeper`) and the 3D *Dovon mash'ali*
(`Projects/BeaconPass`). The stress and UI gates and the export/profiling gates follow in the next items of the package
(docs/TASKS.md).

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
| Shrine | `SaveWrite(Run<n>)` (v4: combat is present), checkpoint, full heal, `LK.Run.Save/Step/Time` |
| Level 1 exit (Gate claimed) | level card → `TransferWrite(Carry)` → `Run.Level=2, Run.Save=0, Entry=carry` → Yertola: `TransferLoad`, heal, autosave `Run2` |
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
sounds, localization, scenes and graphs (`lantern_graphs.py` on `graphkit.py`), and `Assets/Data/oracle.json` (not an
engine asset). Headless tests (`ZaminTests --filter LanternKeeper*`): graphs validate and scenes load; the oracle
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
5.99 MiB, the largest scene 547 KB, model 372 KB, texture 140 KB, sound 516 KB / 12 s) including
`Assets/Data/oracle.json`. Headless tests (`ZaminTests --filter BeaconPass*`, models read on the CPU so the real
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
