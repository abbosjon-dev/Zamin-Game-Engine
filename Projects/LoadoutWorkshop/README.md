# Loadout Workshop

A compiler-free 2D/3D equipment and ability sample. Open `LoadoutWorkshop.zproject` in the Editor
or Player; choose Loadout2D or Loadout3D. [Native API and limits](../../docs/GameplayLoadout.md).

| Key | Action |
|---|---|
| 1 / 2 / 3 | Equip Sword / Plate / Charm from the shared inventory. |
| U | Unequip the weapon (fails safely if the bag cannot accept it). |
| Space | Cast Bolt at the sentry; energy and independent cooldown apply. |
| H / J | Self-heal using a Potion / restore Shield. |
| R | Claim the sentry quest once. |
| F5 / F9 | Save / restore equipment, bag, combat, abilities and quest state. |

Sword grants +20 Attack/+10 Power. Plate grants +100 Armor/+50 energy capacity.
Charm regenerates five energy per game second. Two Potions are initially owned. The actor starts
injured; healing consumes a Potion only when it has an effect. Two equipped Bolt hits defeat the
sentry and unlock a quest reward of five Coins and 150 XP.

The live HUD displays actual native state. Save after the first cast, restart and load: the weapon,
armor, potion count, remaining cooldown and cast history survive. Defeat the sentry, claim and save;
a third process rejects corpse casts, duplicate equip and duplicate quest claims without spending
energy or granting another reward.

`Auto` defaults false. The workflow uses actual keyboard actions in Player and the same command
events in Editor/export automation. `Checks` enables canonical-state assertions; `Phase`/the saved
`Loadout.Phase` preference select only the QA scenario. Generate assets deterministically with
`python3 Tools/samples/gen_loadout_workshop.py`. This is a functional mechanics course using simple
geometry, not a finished art scene or a projectile/effect editor.
