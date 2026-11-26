# Beacon Pass

A legend of the caravan road: tonight a caravan crosses the pass, but the tyrant Black Warden has put out the beacon at
the top of the mountain. The courier Temur climbs from the camp in the valley through the caravanserai to the tower,
gathers three fire stones, defeats the Black Warden and lights the beacon again before dark.

Zamin Engine's compiler-free 3D vertical slice: a title menu, one complete level (the Pass: the camp, the caravanserai,
the stable, the plateau in front of the tower), a third-person hero (character controller + orbit camera), a skinned
humanoid model and Animator locomotion (an idle/walk/run blend, jump and landing, strikes and magic on an upper-body
layer, an additive layer when hit, death), strikes and the fire bolt tied to animation events, the ground slam, AI
guards, archers and a boss, a book of four quests (experience, levels), equipment, saving at shrines, the victory
and defeat screens, music and sound. All of it comes from the `Tools/samples/gen_beacon_pass.py` generator.

| Keyboard / mouse | Gamepad | Action |
|---|---|---|
| WASD / arrows | left stick | Run (hold left Shift — walk slowly) |
| Mouse | right stick | Camera |
| Space | A | Jump |
| LMB / J | X | Strike (stronger with the sabre) |
| RMB / K | RB | Fire bolt (Bolt, sets on fire) |
| Q | Y | Ground slam (from level 2, hits and stuns everyone around) |
| R | D-pad down | Potion |
| E | B | Use (chest, gate, shrine, beacon) |
| I / Tab | Back | Inventory |
| Esc / P | Start | Pause (Resume, Save and quit to menu) |

Quests: **The mountain pass** (defeat the gate guards and open the caravanserai gate), **Fire stones** (three stones),
**The Black Warden** (the boss), **The beacon** (light the beacon on top of the tower) — 800 experience in total, level 4.
Shrines save the game and restore health; after three falls you continue from the last save on the defeat screen.
Demo: `ZAMIN_DEMO=1 ./build/bin/ZaminPlayer Projects/BeaconPass` — a bot plays the game.
Details: [docs/VerticalSlices.md](../../docs/VerticalSlices.md).
