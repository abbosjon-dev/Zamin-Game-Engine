# Changelog

The changes of Zamin Engine by the milestones of the [task plan](docs/TASKS.md): each milestone is 20 tasks, the
newest at the top. The source is the git history: `Tests/changelog.py` splits the commits into milestones
(`Tests/changelog.py --commits M05` — all the commits of one milestone), and the first line under each section (date,
tasks, the number and range of commits) has to be the same as what that script prints — `Tests/docs_check.py` (ctest
`DocsCheck`) checks this. A commit whose subject does not start with `Mnn` belongs to the milestone of the commit
before it; the commits before the 500-task plan are **M00**, the first version of the engine.

Engine version: **0.1.0** (`project(Zamin VERSION ...)`, not released yet). The planned milestone — M25
"Optimization and polish" — is in [docs/TASKS.md](docs/TASKS.md). When work on the tasks of a new
milestone starts, add a section for it ([CONTRIBUTING.md](CONTRIBUTING.md#9-the-task-list-and-the-changelog)).

## M25 Optimization and polish

2026-11-08 · tasks 481–500 · from `670faf9` on (the newest milestone)

- **2D batch order** (`BatchOrder`): sprites / circles / texts that do not overlap on the screen are moved next to
  the batch of their own kind (the picture is the same), rows of sprites with many textures are grouped by texture;
  statistics on why a batch was flushed early (`Flush2DTextures`, `Flush2DKind`, `Flush2DState`). The `mixed2d`
  benchmark: 670 -> 65 draw calls.
- **Per-frame allocations**: measured with `Tests/AllocCounter` (an operator new counter, `ZAMIN_ALLOC_TRACE`) and
  reduced - a frame of 10 000 sprites 10 023 -> 2 allocations, 1 000 meshes 1 504 -> 6 (without instancing 7 105 ->
  6): the world transform cache, the 2D lists and the instancing groups are kept between frames, shader uniform names
  are `std::string_view`, a text layout cache.
- **World transform cache** (finds a change by comparing the values, follows the hierarchy) and a cache of the
  component pools: a repeated `GetWorldTransform` 221 -> 33 ns.
- **Fonts**: baked atlases on disk (`FontAtlasCache`: a 2.4 ms read instead of 45 ms of rasterizing), a glyph table,
  optional **kerning** (GPOS pairs, `.zmeta` `Import.Kerning`, a checkbox in the font inspector).
- **Background loading** (`AsyncLoader`): `TextureLibrary::LoadAsync`, `Model::LoadAsync` - the file is read on a
  worker thread and uploaded to the GPU on the main thread, ~4 ms per frame; the editor opens scenes this way,
  automatic screenshots load immediately.
- **Shader cache** (`ShaderCache`): linked programs as program binaries in `~/.cache/zamin/ShaderBinaries` (key: the
  source text + the driver; a rejected file is deleted, a driver without a format compiles every time).
- **Frame uniform buffer**: the camera / light / shadow / fog / sky values of the mesh shaders in one std140 block
  (`ZaminFrame`, `UniformBuffer`, `Shader::BindUniformBlock`) - one upload per frame.
- **Culling in the shadow pass**: casters whose shadow cannot fall where the camera looks are not drawn either
  (`Renderer3D::ShadowReachesVolume`), statistics per cascade.
- **Parallel**: one large particle system is simulated in chunks on worker threads, marking the animators'
  visibility is parallel (the result is the same). **Editor**: the hierarchy and the content browser cells use
  `ImGuiListClipper` (10 000 entities, 5 000 files).
- **Startup time** (`StartupTimer`, `ZAMIN_STARTUP_PROFILE`, `Tests/startup_bench.py`): the editor and the player
  log the time to the first frame and its parts. Only the used icons are in the editor's atlas (`IconRanges.inl`,
  `Tools/gen_icon_ranges.py`), the Scenes panel reads the files in parallel and opens the pictures in the
  background, the player pre-reads the scene's images in parallel: the editor (Showcase3D) 1 094 -> 750 ms.
- **Memory**: the CPU copies of the ImGui atlas are freed and the atlas is smaller - editor RAM 451 -> 325 MB; the
  player logs its memory on exit (`ZAMIN_PLAYER_STATS`), the startup time is in the Statistics panel.
- **Sample check and report**: the 60 FPS requirement by CPU time (a small window, Low) - 8 / 9 samples were within
  16.6 ms, EndlessRunner was not (155K triangles) - with **LODs of the primitives** (a sphere / cylinder / cone that
  is small on the screen uses a simpler copy) 67K triangles, mean 23 -> 15.3 ms;
  [docs/Performance.md](docs/Performance.md) - the heaviest spots from profiler captures, what was fixed, the
  remaining limits.
- **Translations**: 259 new English pairs, terminology (color), the `EditorTranslationKeysComplete` test (every
  translatable label of the editor code has an English text).
- **Fixes after the review**: in background loading the worker thread does not read the asset registry (import
  settings are taken on the main thread at load time), a queued GPU upload holds its texture and GL objects are
  deleted on the main thread only, `Reload` / `Reimport` wait for a load in progress; a background texture that
  could not be read is let go by sprites and tilemaps (not black); prefab and scene pictures are drawn with fully
  loaded assets (`AsyncLoader::Immediate`); `LoadNow` does not decode images twice; `Tests/bench.sh --compare`
  measures a benchmark that looked slow again (`--repeat`); build folders other than `build/` write game modules to
  their own `GameModules/`.
- **Final regression**: gcc Debug / Release, clang, ASan + UBSan (`Tests/sanitize.sh address,undefined`), all the
  test suites - [docs/Quality.md](docs/Quality.md).

## M24 Sample games

2026-11-08 · tasks 461–480 · 18 commits (`5623496` … `37d2916`)

- New sample projects (each with its own `.zproject`, `Assets/` and C++ script module `Source/`, in the root build):
  **Neon Arena** (`Projects/TopDownShooter`: a twin-stick ship, a dash, 5 kinds of enemies, waves, score and combo,
  bonuses), **Zamin Breakout** (`Projects/Breakout`: a paddle, Box2D ball physics, plain / hard / steel / gold /
  bomb bricks, 6 bonuses, 6 levels) and **FPS Arena** (`Projects/FPSArena`: mouse look, sprint and stamina, jump,
  crouch, a jump pad, a parkour course and a lap record).
- In every game: a main menu (a bot plays behind it), a pause menu, a game-over panel, a SaveData record, project
  input actions (keyboard + mouse + gamepad), Uzbek / English texts (localization) and a demo mode (`ZAMIN_DEMO=1` —
  a bot plays, `Demo: ...` lines are written to the log).
- **FPS Arena** gained raycast shooting (the Blaster: ammo, reload, recoil, a beam trail, sparks), targets
  (standing, sliding, pop-up, a flying drone) and a 60-second "Target practice" mode (centre x2, streak, accuracy,
  record).
- Three more samples: **Warehouse Keeper** (`Projects/Sokoban`: 3D Sokoban, 10 levels, undo, a level menu, a BFS
  solver bot), **Silk Road** (`Projects/EndlessRunner`: a 3D endless runner - three lanes, jump, slide, procedural
  obstacles, coins, a magnet and a shield, growing speed) and **Valley Race** (`Projects/Racing`: arcade car
  physics - steering, grip and drift, a circuit, checkpoints, lap time, 3 bot opponents).
- The editor's launcher shows the sample games as cards with pictures: the new `Description` and `Thumbnail` keys
  of `.zproject` (Project settings > General; `ProjectSerializer::ReadSummary`); the pictures are taken from real
  headless screenshots of the games with `Tools/samples/make_thumbnails.py`.
- All the assets are made by code: `Tools/samples/gen_*.py` (the shared `samplekit.py`) writes the textures, sprite
  atlases, normal maps, `.obj` track meshes, WAV sounds and music, the scene and project files
  ([Tools/samples/README.md](Tools/samples/README.md)). The scenes are compact JSON (a list of numbers on one line).
  The assets of each game are under 2 MiB (Valley Race went from 2.2 MB down to 1.9 MiB).
- Each sample (all nine) has a `README.md`: controls, gameplay, the features shown, a tour of the code, assets and
  tests.
- Tests of the sample games (ctest label `samples`): `Tests/samples_test.py` runs every game headless in
  `ZaminPlayer` and checks four kinds of cases:
  - demo: a bot plays and the log shows a result (score, wave, level, lap, distance, undo);
  - menus: `ZAMIN_PLAYER_INPUT` goes through the main menu → game → pause → continue → main menu flow, and in some
    games the results panel opens too;
  - smoke: the older samples start;
  - in every run: the exit code, no error in the log, the script module loaded, the picture is not a single color.

  `Tests/samples_assets.py` (`SampleAssets`) checks the budget, `.zmeta`, PNG / WAV and references, and with
  `--regen` compares the generator's output byte for byte.
- Engine: `Time::SetCaptureFrameRate` / `ZAMIN_CAPTURE_FPS` — the game clock is fixed (every frame exactly 1/N s),
  so the tests pass the same way on every machine. `Input::SimulateKey` — polling the keyboard does not clear a
  simulated key. The player's `ZAMIN_PLAYER_INPUT` script really holds a key down with it and applies the `hold=` /
  `release=` actions.
- Menus: `Menu: ...` is written to the log for the panels of every game. Fixes:
  - in the main menu of Silk Road and Valley Race "Play" stayed unselected; now it starts with Enter / A;
  - Valley Race can be paused during the start countdown too;
  - Neon Arena plays a sound when the game ends;
  - the Silk Road bot computes the jump point from the speed and the length of the obstacle.
- Records: `SaveData::SubmitRecord(key, result[, RecordOrder::Lower])` and `IsNewRecord` — written when the result
  is better. Six games save their record with it: when the game ends, when leaving from the pause, when restarting
  and when the game is closed too (Valley Race — every lap). `Records: ...` / `New record: ...` are written to the
  log; with `ZAMIN_DEMO_RECORDS=1` the demo's result becomes a record too. `samples_test.py` checks after every demo
  that the record was written to the SaveData file and is read on the next run.
- Control hints: `Input::GetActiveDevice` (the device used last, `Input device: ...` on a switch),
  `Input::GetBindingText`, `Input::ExpandBindings` (`[Jump]`, `[Look|Mouse]`, `[kb:...]`, `[pad:...]`) and the
  **Control hints** (`InputHints`) checkbox of the text component: the text shows the keys of the input map and is
  updated when the device changes; the key names use the `input.<id>` keys of the language files. The main menus of
  the six games work this way. The player's `ZAMIN_PLAYER_INPUT` presses a simulated gamepad too (`pad=A`,
  `padhold=`, `stick=LeftX,0.8`); the `gamepad` cases of `samples_test.py` play the six games with the gamepad only,
  `samples_assets.py` checks the input map (a keyboard and a gamepad binding for every action, scripts read only
  from the map).
- Export: `Tests/samples_export.py` (ctest `SampleExports`) exports the nine samples with the editor
  (`export_test.sh`) and plays the exported games with `samples_test.py --exported`. The bug it found: Neon Arena
  and Zamin Breakout call sounds by their short name (`Sfx("hit")`), and export dropped them as unused — the
  exported game had no sound. `Export.AlwaysInclude: ["Audio/*.wav"]` was added to the two projects, and
  `samples_assets.py` now checks it.
- [docs/Samples.md](docs/Samples.md) — the gallery of the sample games (pictures by `Tools/samples/make_gallery.py`),
  records, controls, tests, export and performance.
- Fixes after the review: the editor's launcher shows the recent scenes and projects before the sample cards again
  (with a history the cards are on one row, with **Show all**); in Warehouse Keeper "Play" opens the player's level,
  not the level of the bot behind the main menu, and presses during the step animation are queued (the
  `Sokoban/menuwait` case of `samples_test.py`); the Silk Road magnet does not depend on the frame rate; Neon Arena
  switches to mouse aiming with the mouse button of "Fire"; the hints of `TextComponent` are updated after an empty
  `Text` too; Project settings > General sees a change of the description and the picture; a dark plate
  (`samplekit.ui_backdrop`) under the tagline and the control hints of four games' main menus, the Breakout hints
  inside the field; clang warnings (unused fields) removed.
- Performance: `Tests/samples_bench.py` (Release, llvmpipe) — frame time, the slowest frame, the number of
  entities, the CPU time of the game code, budgets; the player's `--benchmark` logs the profile of the slowest frame
  and `ZAMIN_PLAYER_STATS` the scene of the last frame, the post-processing passes are separate in the profiler.
  The hitches found were fixed: text is drawn in `Renderer2D` in its own batch with a single-sampler shader (a new
  font atlas no longer makes the driver recompile the 16-slot sprite shader — the slowest frame in Breakout
  2993 → 141 ms), up to 16 font atlases are kept, SSAO reconstructs the view position without a matrix inverse.

## M23 Documentation

2026-11-08 · tasks 441–460 · 10 commits (`a95a849` … `0ecc1ee`)

- The files of `docs/` moved to one naming scheme (PascalCase: `AI.md`, `Animation.md`, `Core.md`, `Prefabs.md`,
  `ProjectSettings.md`, `Runtime.md`, `Scenes.md`, `Testing.md` ...); all links and the comments in the code were
  updated.
- New guides: [Scripting](docs/Scripting.md) (all the public functions of `ScriptableEntity`),
  [Components](docs/Components.md) (79 components; the field tables are generated from the source by
  `Tests/docs_components.py`), [Editor](docs/Editor.md), the [2D](docs/Tutorial-2D.md) and [3D](docs/Tutorial-3D.md)
  first-game tutorials (done in the editor, checked in the player and in an export), [Physics](docs/Physics.md),
  [Rendering](docs/Rendering.md), [UI](docs/UI.md), [Audio](docs/Audio.md), [Architecture](docs/Architecture.md),
  [BuildAndExport](docs/BuildAndExport.md), [FAQ](docs/FAQ.md); [Animation](docs/Animation.md) was extended.
- API reference: the `Doxyfile` (output in `build/docs`, the `docs` target in CMake) and Doxygen comments in the
  main headers (`Application`, `Entity`, `Scene`, `ScriptableEntity`, `SceneManager`, `Renderer2D` / `Renderer3D`,
  `AudioEngine`, `PhysicsWorld3D`, `Project` and the main components).
- Documentation checks: `Tests/docs_check.py` (ctest `DocsCheck`: links and anchors, components, the script API,
  player options, the messages of the FAQ in the source, CMake options, the CHANGELOG, the documentation index,
  image sizes) and `Tests/docs_snippets.py` (ctest `DocsSnippets`: the whole script classes of the documentation
  compile).
- The bugs the checks found were fixed: the missing OGG decoder (the documentation and the comment of
  `AudioSourceComponent::Path`), a wrong sample scene name, a `GetTransform()` that scripts do not have, examples
  that gave warnings.
- [CONTRIBUTING.md](CONTRIBUTING.md), this CHANGELOG, the [documentation index](docs/README.md); the README became
  a short front page (the full feature list moved to [docs/Features.md](docs/Features.md)); headless editor / player
  screenshots in the guides.

## M22 Quality and tests

2026-11-08 · tasks 421–440 · 16 commits (`f5c652c` … `9cdfff1`)

- Sanitizer builds: `ZAMIN_SANITIZE=address|undefined|leak|thread`, the presets of `CMakePresets.json`,
  `Tests/sanitize.sh` (the unit tests; with `--smoke` 3 games and the editor). ASan found a heap-use-after-free in
  the audio mixer (the children of a deleted group); Clang UBSan — the miniaudio implementation moved to its own TU.
- Our targets are free of warnings with `-Wall -Wextra -Wpedantic` on GCC (Debug / Release) and Clang.
- The rendering regression test in CI and with a local script (`Tests/render_tests.sh`), the editor smoke test
  (`ZAMIN_EDITOR_OPEN_ALL_PANELS`), the script hot reload test, the export test (the exported game with a
  screenshot), the physics determinism test (Box2D and Jolt bit for bit the same), the render benchmark (10 000
  sprites, 1 000 meshes).
- Test framework: a filter by name (glob, excluding with `-`), `--list`, a timing report, JSON and JUnit reports,
  `--fail-fast`. `ZaminBench --ecs` and an extended `--serialize`, `Tests/bench.sh --compare` (the quadratic search
  in the `DestroyEntityDeferred` queue was fixed).
- Code coverage (`ZAMIN_COVERAGE`, `Tests/coverage.sh`, a report by module), `.clang-format` and
  `Tests/format_check.sh` (changed lines only), the `sanitizers`, `format` and `coverage` jobs in CI and the game
  build artifact (`Tests/package_games.sh`).
- [docs/Testing.md](docs/Testing.md) (all the tests, the catalogue of regression tests) and
  [docs/Quality.md](docs/Quality.md) (the measured results).
- Fixes: `Scene::Copy` copies the components in hierarchy order (the order of the scripts on Play in the editor is
  the same as in an exported game), the script module copies of dead processes are cleaned up.

## M21 Prefabs and serialization II

2026-11-07 – 2026-11-08 · tasks 401–420 · 22 commits (`1d18e96` … `bf6c809`)

- Prefab instances are linked to the prefab (`PrefabLinkComponent`): overrides are marked in the inspector, Apply /
  Revert at every level, unlinking, nested prefabs (`PrefabDatabase`, `PrefabUtility`, `PrefabOverrides`).
- Prefab mode (separate editing: a breadcrumb, Save / Close, nested levels), prefab variants, prefab thumbnails in
  the content browser, dragging several prefabs in together.
- `EntityRef` references are remapped to the new copy on duplication / in a prefab; scene format v5 and the
  migration chain; partial recovery of a broken scene (`SceneRecovery`).
- The serialization fuzz test (`SerializationFuzz`), `PrefabCache` (a prefab file is read once) and
  `ZaminBench --serialize --prefabs`.
- Prefab hot reload (a file changed outside is carried over to the instances), the clipboard format is the same as
  a prefab, the prefab check on export (`PrefabExport::Prepare`, rules R1–R10), [docs/Prefabs.md](docs/Prefabs.md).

## M20 Navigation and AI

2026-11-07 · tasks 381–400 · 18 commits (`01667d6` … `764a9ab`)

- The AI module: NavGrid (cell cost, height, erosion) and A* (a binary heap, partial paths), path smoothing;
  `NavGrid2DComponent` (from tilemaps and colliders), `NavGrid3DComponent` (the floor by rays), `NavAgentComponent`.
- Dynamic obstacles (`NavObstacleComponent`, repathing), steering behaviours and obstacle avoidance with rays, the
  AI state machine, `SightSensorComponent` (a view cone and occluders), `PatrolPathComponent`, `FormationComponent`
  (line, column, wedge, circle), a minimal behavior tree.
- Navigation debug drawing (Gizmos > Navigation in the editor, `--debug-nav` in the player), nav paths in parallel
  on the job system, `ZaminBench --nav`, saving the bake result in a `.znav` file.
- Samples: AI3D, Squad3D (Showcase3D), Chase2D (Platformer2D), the island guard of Adventure3D;
  [docs/AI.md](docs/AI.md).

## M19 Math and core

2026-11-07 · tasks 361–380 · 12 commits (`ffc14bd` … `70bbb3b`)

- The noise module (Perlin, Simplex, Value, fBm, ridged, turbulence), the `AnimationCurve` type (script field,
  scene file, inspector), splines (`SplineComponent`, `SplineFollowerComponent`, the editor gizmo), color utilities.
- Math helpers (`Remap`, `InverseLerp`, `SmoothDamp`, `MoveTowards`, angles) and quaternions (`LookRotation`,
  `FromToRotation`, `AngleAxis` ...), `Math/Geometry.h` (AABB, sphere, ray, plane, intersections).
- The job system (`JobSystem`, `ParallelFor`; `--jobs`, `ZAMIN_JOBS`) — particles and animator poses on worker
  threads; memory statistics (`MemoryStats`).
- Log levels (per logger, `--log-level`, `ZAMIN_LOG_LEVEL`), asserts with messages (`ZM_ASSERT`, `ZM_VERIFY`),
  `Stopwatch` / `ScopedTimer`, `StringUtils`, file system helpers, `Scene::ScreenPointToRay` and `GetMouseRay` in
  scripts; the Splines sample scene, [docs/Core.md](docs/Core.md).

## M18 Runtime player

2026-11-07 · tasks 341–360 · 12 commits (`1fb9950` … `16cedfc`)

- The player's window settings with an order of precedence (command line > what the player saved > the project;
  `PlayerLaunchSettings::Resolve`), F11 / Alt+Enter fullscreen, the FPS limit (`FramePacer`, `--fps`), a splash
  screen, the command line (`--help`).
- The player's log file (`LogFile`, `latest.log`, `--log-file`), the crash handler (`CrashHandler`: signals, a
  report, a dialog), the `Cursor` API for scripts, window resizing and letterboxing, pause on focus loss, gamma and
  brightness.
- Export: unused assets are left out (`ExportAssets::Plan`), the script module is built in Release and stripped, a
  relocatable `.desktop` and 16…256 px icons, the export report dialog (`export-report.json` / `.txt`).
- The headless performance test (`--benchmark N`), the automatic export test, a game menu sample
  (`Prefabs/GameMenu.zprefab`), version information, [docs/Runtime.md](docs/Runtime.md).

## M17 Project settings

2026-11-07 · tasks 321–340 · 10 commits (`d845846` … `d72dd56`)

- The project settings as a window split into sections (with search): the input map, tags and layers, presets of
  the physics layers, quality levels (Low … Ultra), the player window, the game icon, company / product / version,
  the start and build scenes, a versioned `.zproject`.
- Graphics context settings (OpenGL version, MSAA, sRGB, debug context; `--gl`, `--msaa`, `--gl-debug`, `--srgb`),
  time settings and the `Time` API, default gravity, audio defaults.
- Localization: `Assets/Localization/<language>.json` (nested keys, plural forms), the script API, a key in the
  text component, a table in the editor; Platformer2D in three languages.
- The language of the editor's interface (Uzbek / English, `EditorText::Tr`),
  [docs/ProjectSettings.md](docs/ProjectSettings.md).

## M16 Editor II

2026-11-06 – 2026-11-07 · tasks 301–320 · 9 commits (`4195bb4` … `85f8c51`)

- A color tint of the editor in play mode, size presets of the game view (letterbox), search in the console (text /
  regex) and collapsing identical messages, the Settings window (accent color, font size, camera defaults).
- The keyboard shortcut table (rebinding, conflicts), autosave and recovery, recent scenes, the undo history window
  and undoing the selection.
- Window layouts (Default, 2D, Animation, 4 views and your own), additional scene views, orthographic axis views
  (numpad), the editor camera settings, entity names in the viewport.
- The scene statistics window, opening code in an external editor, the compile errors window (GCC / Clang / linker /
  CMake output is parsed), fewer frames while idle.

## M15 Scene management

2026-11-06 · tasks 281–300 · 15 commits (`8e40f55` … `58cc9bc`)

- `SceneManager`: additive scenes, `DontDestroyOnLoad`, background loading (`LoadSceneAsync`), fade transitions; the
  build scene list and loading by index (`LoadNextScene`, `ReloadScene`), the player's `--scene <index>`.
- Scene events (`OnSceneLoaded` / `OnSceneUnloaded` / `OnActiveSceneChanged`), name / tag indices, the load errors
  window, a stable file order, the scene format version and migration.
- Scene metadata (description, author, tags, picture) and the Scenes window of the editor; global state
  (`GameState`) and services (`Services`); the load time by stage in the profiler.
- Faster serialization and a binary cache (`.zsceneb`) for large scenes, sub-scenes (`SubSceneComponent`); the
  menu → loading screen → level flow in Platformer2D, [docs/Scenes.md](docs/Scenes.md).

## M14 Particles II

2026-11-06 · tasks 261–280 · 7 commits (`e09b523` … `9abd564`)

- The particle simulation in a module of its own (`ParticleSystem`): a multi-stop color gradient, a size curve,
  velocity over lifetime, texture sheet animation, cone / edge shapes.
- Emission from a mesh surface, sub-emitters, collision, noise (turbulence), trails.
- GPU instancing, sorting of transparent particles, prewarm, a burst table, simulation speed / duration / delay /
  stop action.
- A library of ready-made effects (`.zparticle`), weather around the camera (rain, snow), inspector modules; the
  ParticlesII–IV sample scenes.

## M13 Animation II

2026-11-06 · tasks 241–260 · 16 commits (`009d765` … `6c68a90`)

- Property animation clips (`.zanim`) and `AnimationPlayerComponent`, animation events (`OnAnimationEvent`), root
  motion; the Animation window (timeline, dope sheet, curves, record mode).
- Animator: the 1D blend tree, layers (avatar mask, override / additive), the Animator graph window, the live state
  in the inspector; sprite animation clips (`.zsanim`).
- Previewing a skeletal clip in the editor (scrub), `BoneAttachmentComponent`, `FootstepsComponent`, trimming and
  splitting clips, glTF morph targets, several skins in one glTF.
- Animation caching (the pose cache, culling off screen), tests, the Adventure3D gate sample,
  [docs/Animation.md](docs/Animation.md).

## M12 UI system

2026-11-06 · tasks 221–240 · 14 commits (`59d32a6` … `dbc2359`)

- UI widgets: Image, Slider, Toggle, Progress Bar, Input Field; the UI hierarchy, stretch anchors and the pivot,
  Canvas scaling, layout groups (row / column / grid).
- UI events (`OnPointerEnter` / `Exit` / `Down` / `Up` / `Click`), keyboard and gamepad navigation.
- Text: font atlases per size, alignment, word wrapping, overflow modes; UI animations (`TweenUI`, `UIScaleTo`,
  `UIFadeTo` ...).
- Selecting by the UI rectangle and the rect gizmo in the editor; UI Mask and Scroll View; a pause menu sample and
  the game time scale.

## M11 Audio

2026-11-06 · tasks 201–220 · 13 commits (`b7e1206` … `adeed7d`)

- Audio mixer groups (Master / Music / SFX / UI and the project's own: fader, mute, solo, level meter), the Audio
  Mixer window, `AudioListenerComponent`, music crossfade, streaming.
- Volume / pitch variation, the voice limit (priority), the Audio Source script API, the Doppler effect, audio clip
  import settings, fades, filters.
- The audio profiler, the waveform in the asset inspector (zoom, scrub, loop range), the directional cone for 3D
  sound, `AmbientZoneComponent`, the `OnSoundFinished` callback; the Audio sample scene.

## M10 3D physics II

2026-11-05 – 2026-11-06 · tasks 181–200 · 15 commits (`c92e002` … `a06d581`)

- 3D joints: Fixed, Hinge, Distance, Slider, Cone (breaking and `OnJointBreak3D`), editing the anchors in the
  viewport.
- Mesh Collider 3D, 3D collision layers (the matrix shared with 2D), overlap and shape cast queries (`Raycast3DAll`,
  `SphereCast3D` ...), CCD, the 3D sleep API, 3D physics materials (`.zphysmat`).
- The physics debug view: contact points (normal, depth, impulse), joints, velocities.
- Samples: Queries3D, Physics3D, Ragdoll3D, PhysicsDebug3D, Joints3D.

## M09 2D physics II

2026-11-05 · tasks 161–180 · 16 commits (`4208631` … `262f0a0`)

- 2D physics was split out into `PhysicsWorld2D`; 2D joints (Distance, Revolute, Prismatic, Weld, Wheel), breaking
  and `OnJointBreak`.
- Polygon / Edge Collider 2D (editing the points in the viewport), the physics material asset (`.zphysmat`), 2D
  collision layers, Platform / Area / Buoyancy Effector 2D.
- `OverlapCircle2D` / `OverlapBox2D` / `OverlapPoint2D`, `Raycast2DAll`; Box2D sub-steps and sleep settings.
- Samples: Joints2D, Shapes2D, Effectors2D, Ragdoll2D.

## M08 2D improvements

2026-11-05 · tasks 141–160 · 17 commits (`4c05130` … `c1a688c`)

- The sprite editor and the sprite atlas (named frames in `.zmeta`), 9-slice and tiled sprites, sorting layers,
  parallax.
- `Camera2DFollowComponent` and camera shake (`ShakeCamera`), tilemap layers, auto-tile rules, painting tools and
  the palette, tile properties (collision, one-way, animated tiles).
- Sprite flash and outline, color tint animation (`TintAnimationComponent`), 2D lighting (`Light2DComponent`), the
  pixel-perfect camera, sorting by Y; the Tilemap2D and TopDown2D samples.

## M07 Debug draw and visualization

2026-11-05 · tasks 121–140 · 8 commits (`5deeafd` … `6418441`)

- The `DebugDraw` API (line, ray, arrow, box, sphere, circle, path, text; color, duration, depth test).
- Editor gizmos: the camera frustum, the light direction, 3D audio distances, the gizmo size, the shader-based
  selection outline, bounds (bounding box), skeleton bones, vertex normals.
- The viewport statistics overlay (FPS graph, frame time, draw calls, culling).

## M06 Rendering I

2026-11-05 · tasks 101–120 · 7 commits (`a9e3ae6` … `df7b5ae`)

- The spot light, up to 32 point + spot lights, FXAA, the render scale, auto exposure.
- Wireframe and the debug views (lighting only, normals, albedo, roughness, metallic), mesh and 2D frustum culling,
  GPU instancing, render statistics, shadow quality settings.
- Several suns, the sky gradient and the sun disc, chromatic aberration, film grain, bloom tint and lens dirt; the
  rendering regression test (`ZaminRenderTests`).

## M05 Materials (asset)

2026-11-05 · tasks 081–100 · 9 commits (`ebb68c7` … `7210b63`)

- Material assets (`.zmat`: shader, blending, alpha cutoff, double-sided, PBR values, textures by UUID),
  `MaterialAsset` and `MaterialLibrary` (a cache, reloading in place).
- Surface types: transparent (sorted back to front), cutout, double-sided; the priority of transparents.
- Dragging a material onto the viewport / hierarchy, a new material in the content browser, exporting glTF
  materials to `.zmat`, material instances in scripts (`GetMaterial` / `GetSharedMaterial`), the library of default
  materials.

## M04 Asset system

2026-11-05 · tasks 061–080 · 16 commits (`847d99e` … `c8a6ee0`)

- The asset module: `AssetHandle` with a UUID, `.zmeta` files (UUID, type, import settings), `AssetRegistry`, the
  file watcher (inotify) and reloading images / fonts / HDRIs / models / materials in place.
- Asset references in scene and prefab files by UUID (a moved file is found), the registry cache
  (`.zamin/AssetRegistry.json`, `AssetRegistry.zreg` in an export).
- The asset inspector (image, model and audio previews and import settings), the index of usages, unused assets, a
  warning before deleting, import errors (`AssetImportLog`).
- Search by the full path and a thumbnail cache in the content browser.

## M03 Active/inactive entities

2026-11-04 – 2026-11-05 · tasks 041–060 · 5 commits (`3c48c13` … `b8f593a`)

- `TagComponent::Active` and activity along the hierarchy: an inactive entity is not drawn, is taken out of physics
  and its script gets `OnDisable`; `SetActive`, `IsActive`, `IsActiveInHierarchy`, `IsActiveAndEnabled` in scripts.
- Particles, audio, the Animator, UI buttons, cameras, lights, tilemap collision and the Character Controller stop
  on inactive entities; prefab instances keep the active state; `Alt+Shift+A` in the editor.

## M02 Editor UX I

2026-11-04 · tasks 021–040 · 5 commits (`224ea5a` … `42d17b3`)

- Multi-selection (`Ctrl` / `Shift`), reordering by dragging in the hierarchy, the Transform menu (reset / copy /
  paste).
- Snapping and grid settings, camera bookmarks (`Ctrl+1..5` / `1..5`), `Shift+F`, a copy with `Alt`+drag.
- The order of the components in the inspector and copy / paste, the eye and the lock in the hierarchy (`H` /
  `Alt+H`).
- Content browser: moving by dragging onto a folder (references are updated), the list view, the type filter,
  importing from outside; the status bar.

## M01 Scripting and gameplay API

2026-11-04 · tasks 001–020 · 6 commits (`12d1b95` … `dda0c61`)

- The 500-task plan ([docs/TASKS.md](docs/TASKS.md)); `EntityRef` fields and their inspector, timers (`Invoke`,
  `InvokeRepeating`) and tweens.
- The input action map (in the project settings), `Input::IsActionPressed` / `IsActionJustPressed` / `GetAxis`, the
  key state between frames, `SaveData`.
- The `Random` API (PCG32), tags, messages and the event bus (`SendMessage`, `Broadcast`, `Emit`), hierarchy
  queries, the `FindEntity` cache.
- The script life cycle (`OnEnable` / `OnStart` / `OnDisable`), `OnTriggerBegin` / `OnTriggerEnd`, field attributes
  (`ZM_FIELD_ENUM`, `ZM_FIELD_COLOR`, `ZM_FIELD_RANGE`, `.Tooltip`), new-script templates.

## M00 The first version

2026-11-04 · 13 commits (`62eaca8` … `1c8492e`)

- The first complete version for Linux: the editor, the 2D / 3D renderer, 2D physics (Box2D), audio, C++ scripts and
  hot reload, export.
- The hierarchy, prefabs, Undo / Redo, 3D physics (Jolt), sprite animation; the particle system, post-processing
  (bloom), screen UI and buttons.
- The HDR pipeline, glTF models, normal maps, the tilemap, 3D sound; skeletal animation, the Animator, the CPU
  profiler.
- Character Controller 3D, animation crossfade and the Adventure3D game; cascaded shadow maps and fog, SSAO, the
  animation state machine, the HDRI sky and IBL.
