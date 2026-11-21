# Combat Workshop

A compiler-free 2D/3D combat slice: projectiles, area damage, timed status effects, combat HUD feedback and
v4 saves that keep effects and projectiles in flight. Open `CombatWorkshop.zproject` in the Editor or Player;
choose Combat2D or Combat3D. [Native API and limits](../../docs/GameplayCombat.md).

| Key | Gamepad | Action |
|---|---|---|
| Space | A | Bolt (projectile, Burn) at the Sentry: Amount 20 + Attack, 10 energy, 0.5 s cooldown. |
| L | X | Lance along +X: pierces two targets (three hits), Shock stuns for 1 s. 15 energy. |
| N | Y | Nova around you: 12 + Attack to every enemy within 3.5 m in line of sight, Frost slows. 20 energy. |
| M | B | Mend: Regen heals 4 every 0.5 s for 3 s. 10 energy. |
| R | RB | Rally: Haste for 5 s (+10 Attack, 1.5x speed). 20 energy. |
| F5 / F9 | Start / Back | Save / load (save v4: effects, projectiles in flight, loadout, quest). |
| WASD | left stick | Move (Frost slows, Shock stops the motor). |

The red Sentry spits Venom at you whenever its 2 s cooldown allows: 6 damage (absorbed by the shield) and
Poison, which pierces the shield (2 every second, three ticks, up to three independent instances). Three
dummies stand in a line for the Lance; the purple target behind the grey wall is inside the Nova radius but
out of sight, so Nova never reaches it. Energy has no regeneration, so the energy readout is an exact record
of what was spent. Killing the Sentry (a Burn tick may land the kill) completes the quest: 5 Coins and 150 XP,
claimed automatically once.

The HUD shows canonical native state only: health, shield and energy, Attack (Haste included), the Bolt
cooldown and cast count, projectiles in flight, the Sentry and target health, effect rows with stacks
(Poison, Burn, Frost, Shock, Regen, Haste), a hit marker and a four-row combat feed of incoming and outgoing
hits. Projectile visuals come from pooled visual-only prefabs (`Assets/Prefabs`).

Save while a Bolt is still flying and Poison is ticking, restart and load: the Bolt continues from its saved
position and hits, Poison resumes with its remaining ticks, Burn keeps its stacks and tick phase, and Haste
its remaining time. A third process rejects a Bolt at the dead Sentry and an unaffordable Nova without
spending energy or granting the reward again.

`Auto` defaults false. The workflow uses real keyboard and gamepad input in Player and the same command
events in Editor/export automation. `Checks` compares canonical native state with the generator's oracle;
`Phase` / the saved `Combat.Phase` preference select the QA scenario, and outside phase 0 the Sentry does
not cast (a resumed QA run must not depend on how long the restart took). Generate the project
deterministically with `python3 Tools/samples/gen_combat_workshop.py`; acceptance:
`python3 Tests/combat_workflow_test.py --exports`. This is a functional mechanics course with simple
geometry, not a finished art scene or an effect editor.
