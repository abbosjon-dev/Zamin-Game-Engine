# Lantern Keeper

A legend of the old fortress: Soya Bek stole the Lamps of Dawn and the fortress was left in an endless dusk. The young
lantern keeper Nilufar takes the first lamp back from the bandits in the orchards and the other two from the crypt
beneath the fortress, defeats Soya Bek and lights the Dawn Lamp on the altar.

Zamin Engine's compiler-free 2D vertical slice: a title menu, two levels (the Orchards, the Crypt), a hero motor,
strikes and magic (fire bolt, light wave, mend, potion), AI enemies (bandits, archers, skeletons, spirits, a boss), a
quest book (experience, levels, abilities that unlock), equipment and an inventory, a HUD, pause, the level card, the
victory and defeat screens, music and sound. All of it comes from the `Tools/samples/gen_lantern_keeper.py`
generator.

| Keyboard | Gamepad | Action |
|---|---|---|
| WASD / arrows | left stick | Move (Shift / LT — faster) |
| J / LMB | X | Strike |
| K / RMB | RB | Fire bolt (Bolt, sets on fire) |
| L | Y | Light wave (Nova, from level 2) |
| H | LB | Mend (from level 3) |
| Q | D-pad down | Potion |
| E | A | Use (chest, shrine, gate, altar) |
| I / Tab | Back | Inventory (Equip, Drink) |
| Esc / P | Start | Pause (Resume, Inventory, Save and quit to menu) |

Shrines save the game and restore health; after three falls you continue from the last save.
The hero's state (health, bag, equipment, quests, experience) moves from the first level to the second in a transfer
document. Demo: `ZAMIN_DEMO=1 ./build/bin/ZaminPlayer Projects/LanternKeeper` — a bot plays the game.
Details: [docs/VerticalSlices.md](../../docs/VerticalSlices.md).
