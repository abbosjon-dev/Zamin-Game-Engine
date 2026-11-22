# Vertical slices

Two small finished games built only from the engine's gameplay packages and played end to end by a deterministic
demo pilot for acceptance. This page covers the 2D slice; the 3D slice (*Dovon mash'ali*, `Projects/BeaconPass`) and
the stress/UI gates follow in the next items of the package (docs/TASKS.md).

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
