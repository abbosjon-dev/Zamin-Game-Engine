# Zamin Engine documentation

All of Zamin Engine's documentation in one place. Everything is in English; the code examples come from the engine's real API and
the sample games (`Projects/Platformer2D`, `Projects/Showcase3D`, `Projects/Adventure3D`) and
`ctest --test-dir build -L docs` checks that they match the code ([Testing.md](Testing.md#10-compile-time-checks)).
The home page is the [README](../README.md).

**Where to start:**

- **New to the engine?** [README](../README.md) → [build](BuildAndExport.md#2-building-the-engine) →
  [your first 2D game](Tutorial-2D.md) or [your first 3D game](Tutorial-3D.md) → [Editor](Editor.md).
- **Writing a game?** [C++ scripts](Scripting.md) and [components](Components.md), then the guide of the system you need
  (the table below); when something goes wrong — the [FAQ](FAQ.md).
- **Want to change the engine itself?** [Architecture](Architecture.md) →
  [the contribution rules](../CONTRIBUTING.md) → [tests](Testing.md).

## Getting started

| Document | What it is about |
|---|---|
| [README](../README.md) | The home page: the features in brief, screenshots, quick start |
| [Tutorial-2D.md](Tutorial-2D.md) | Your first 2D game "Coins": a hero, coins, a score — step by step in the editor, with the complete code |
| [Tutorial-3D.md](Tutorial-3D.md) | Your first 3D game "Crystals": walking and jumping, a follow camera, crystals, restarting |
| [Editor.md](Editor.md) | The editor: the launcher, windows, menus, the viewport, the inspector, the content browser, Play mode, shortcuts, automation |
| [FAQ.md](FAQ.md) | Frequently asked questions and problems — with the engine's real messages |
| [Features.md](Features.md) | The full, detailed list of features (editor, engine), the editor controls, the roadmap |
| [Samples.md](Samples.md) | The sample games gallery: pictures of nine games, what they show, records, controls and gamepad, tests, export, performance |

## Making a game

- [Terrain.md](Terrain.md): the 3D map, relief brushes, textures, planting forest/prefabs and the heightmap.


| Document | What it is about |
|---|---|
| [Scripting.md](Scripting.md) | C++ scripts: `ScriptableEntity`, `ZM_FIELD` fields, the life cycle, callbacks, the API reference, hot reload |
| [Components.md](Components.md) | The reference of all components: purpose, menu path, scene key, field table |
| [Rendering.md](Rendering.md) | Cameras, 2D (sprite, atlas, tilemap, 2D lighting), 3D (mesh, glTF, materials, lighting, shadows, sky), post-processing, particles |
| [Physics.md](Physics.md) | 2D (Box2D) and 3D (Jolt) physics: bodies, colliders, joints, effectors, the Character Controller, queries, layers |
| [VerticalSlices.md](VerticalSlices.md) | The compiler-free vertical slices: Lantern Keeper (2D, two levels, menu, boss, transfer between levels, demo pilot, headless oracle) and Beacon Pass (3D, character controller, orbit camera, Animator locomotion, combat/AI/quests, shrine saves) |
| [GameplayQuest.md](GameplayQuest.md) | Objective/prerequisite, atomic rewards/XP, quest HUD and v1/v2 saved progression |
| [GameplayLoadout.md](GameplayLoadout.md) | Equipment ownership, combat stats, energy/abilities, consumables and v3 saves |
| [GameplayCombat.md](GameplayCombat.md) | Bounded 2D/3D projectiles, area damage/support and timed status effects: fixed-step flight and ticking, hit ownership/teams, stacking/expiration, stat/speed/stun modifiers, pooled visuals, Blueprint nodes, save v4, the compiler-free CombatWorkshop slice and `ZaminBench --combat` |
| [GameplayUI.md](GameplayUI.md) | Live HUD, inventory pages, modal focus/pause ownership and keyboard/gamepad workflows |
| [GameplaySave.md](GameplaySave.md) | Stable-key health/inventory/interaction/AI saves, atomic slots, backup recovery and fresh-process prefab resume |
| [GameplayAI.md](GameplayAI.md) | Patrol/chase/investigate/attack/return, hostile LOS, nav recovery and 2D/3D NPC prefab courses |
| [GameplayRules.md](GameplayRules.md) | Shared health/shield, melee, death/respawn, inventory, transactional interaction and combat courses |
| [Gameplay.md](Gameplay.md) | 2D/3D motor, follow/orbit camera, actual velocity Animator binding, persistent checkpoints and playable courses |
| [Animation.md](Animation.md) | Property clips and the timeline, skeletal animation, the Animator, 1D/2D blends, Blueprint control, layers, root motion, sprite clips, tweens |
| [UI.md](UI.md) | Screen UI: anchors and layout, widgets, text, events, navigation, UI animations, the pause menu |
| [Audio.md](Audio.md) | The Audio Source and one-shot sounds, 3D sound, mixer groups, music, filters, ambient zones |
| [Scenes.md](Scenes.md) | Loading scenes, additive and background loading, transitions, the build list, events, global state, sub-scenes |
| [Prefabs.md](Prefabs.md) | Prefabs: linked instances, overrides, Apply / Revert, nested prefabs, variants, prefab mode |
| [AI.md](AI.md) | Navigation and AI: NavGrid, A*, the Nav Agent, obstacles, steering, sight, patrol, formations, behavior trees |
| [ProjectSettings.md](ProjectSettings.md) | Project settings: the player window, quality levels, graphics, time, physics, audio, input, tags, localization |
| [Core.md](Core.md) | Math and utilities: geometry, noise, splines, colours, Random, the job system, logging, asserts, the file system |
| [Runtime.md](Runtime.md) | Zamin Player: start-up, the command line, the precedence of settings, log and crash files, the game menu sample |

## Build, export and distribution

| Document | What it is about |
|---|---|
| [BuildAndExport.md](BuildAndExport.md) | Requirements, CMake options and presets, building a game module, Doxygen, export, packaging, distribution |
| [Runtime.md, section 11](Runtime.md#11-export) | An exported game from the player's side |

The API reference (Doxygen, from the comments in the headers): `cmake --build build --target docs` →
`build/docs/html/index.html` ([BuildAndExport.md, section 7](BuildAndExport.md#7-the-api-reference-doxygen)).

## Internals and contributing

| Document | What it is about |
|---|---|
| [Architecture.md](Architecture.md) | Targets, the code layout, the main loop, a scene frame, the script module, the renderer, physics, audio, threads, adding a new component |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | Building, code style, English texts and `Tr()`, tests, sanitizers, documentation and pictures, the commit rules |

## Quality and tests

| Document | What it is about |
|---|---|
| [Testing.md](Testing.md) | All the tests and when to run them, the regression tests, CI, the unit test catalog |
| [Quality.md](Quality.md) | The quality report: the measured results of tests, sanitizers, coverage, warnings, formatting and benchmarks |
| [Coverage.md](Coverage.md) | Code coverage (gcov): the build, the report, the results by module |
| [Benchmarks.md](Benchmarks.md) | The benchmark programs and the Release baselines, comparing |
| [Performance.md](Performance.md) | The performance report: the heaviest spots from profiler recordings, what M25 fixed, the remaining limits |

## Project

| Document | What it is about |
|---|---|
| [TASKS.md](TASKS.md) | The plan of 500 tasks (25 milestones), with the finished ones marked |
| [CHANGELOG.md](../CHANGELOG.md) | The history of changes by milestone (from the git history) |
| [LICENSE](../LICENSE) | The MIT licence |

## Screenshots

The pictures in the guides are real screenshots taken headless from the editor and the player (how they are taken:
[CONTRIBUTING.md, section 7](../CONTRIBUTING.md#7-documentation-and-pictures)). Click a picture to open it at full size.

| | | |
|---|---|---|
| [<img src="images/editor-new-project.jpg" width="300" alt="New project">](Editor.md#1-starting-and-the-launcher)<br>[Editor: a new project](Editor.md#1-starting-and-the-launcher) | [<img src="images/editor-asset-inspector.jpg" width="300" alt="Asset inspector">](Editor.md#7-the-content-browser)<br>[Editor: the asset inspector](Editor.md#7-the-content-browser) | [<img src="images/scripting-inspector-fields.jpg" width="300" alt="Script fields">](Scripting.md#4-inspektor-maydonlari-zm_field)<br>[Script fields](Scripting.md#4-inspektor-maydonlari-zm_field) |
| [<img src="images/scripting-new-script-dialog.jpg" width="300" alt="New script">](Scripting.md#2-birinchi-skript)<br>[A new C++ script](Scripting.md#2-birinchi-skript) | [<img src="images/components-inspector.jpg" width="300" alt="Components">](Components.md)<br>[Components in the inspector](Components.md) | [<img src="images/tutorial-2d-game.jpg" width="300" alt="2D tutorial">](Tutorial-2D.md)<br>[The 2D tutorial game](Tutorial-2D.md) |
| [<img src="images/tutorial-3d-game.jpg" width="300" alt="3D tutorial">](Tutorial-3D.md)<br>[The 3D tutorial game](Tutorial-3D.md) | [<img src="images/rendering-material-gallery.jpg" width="300" alt="Materials">](Rendering.md)<br>[Rendering: materials](Rendering.md) | [<img src="images/physics-debug-editor.jpg" width="300" alt="Physics debug">](Physics.md)<br>[The physics debug view](Physics.md) |
| [<img src="images/animation-timeline-editor.jpg" width="300" alt="Animation window">](Animation.md)<br>[The Animation window](Animation.md) | [<img src="images/ui-layout-editor.jpg" width="300" alt="UI">](UI.md)<br>[UI layout](UI.md) | [<img src="images/audio-mixer-editor.jpg" width="300" alt="Audio mixer">](Audio.md)<br>[Audio mixer](Audio.md) |
| [<img src="images/scenes-panel-editor.jpg" width="300" alt="Scenes window">](Scenes.md#16-editorda-sahnalar)<br>[The Scenes window](Scenes.md#16-editorda-sahnalar) | [<img src="images/prefab-mode-editor.jpg" width="300" alt="Prefab mode">](Prefabs.md#7-prefab-rejimi)<br>[Prefab mode](Prefabs.md#7-prefab-rejimi) | [<img src="images/ai-navigation-play.jpg" width="300" alt="Navigation">](AI.md#14-debug-chizish)<br>[The navigation gizmo](AI.md#14-debug-chizish) |
| [<img src="images/project-settings-input.jpg" width="300" alt="Project settings">](ProjectSettings.md#2-loyiha-sozlamalari-oynasi)<br>[Project settings: Input](ProjectSettings.md#2-loyiha-sozlamalari-oynasi) | [<img src="images/core-splines-editor.jpg" width="300" alt="Spline">](Core.md#6-animationcurve-easing-and-spline)<br>[Splines](Core.md#6-animationcurve-easing-and-spline) | [<img src="images/runtime-game-menu.jpg" width="300" alt="Game menu">](Runtime.md#10-the-game-menu-sample)<br>[The game menu (player)](Runtime.md#10-the-game-menu-sample) |
| [<img src="images/profiler-play-editor.jpg" width="300" alt="Profiler">](Architecture.md#3-dastur-hayoti-va-asosiy-sikl)<br>[The profiler and the main loop](Architecture.md#3-dastur-hayoti-va-asosiy-sikl) | [<img src="images/export-report-editor.jpg" width="300" alt="Export report">](BuildAndExport.md)<br>[The export report](BuildAndExport.md) | [<img src="images/compile-errors-editor.jpg" width="300" alt="Compile errors">](FAQ.md)<br>[Compile errors](FAQ.md) |

- [Visual Script — 2D/3D gameplay without code](VisualScripting.md)

- [The built-in C++ code editor](CodeEditor.md)
