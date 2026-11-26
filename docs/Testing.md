# Tests and regression tests

All the automatic checks of Zamin Engine, what each one guards and how it is run. Every test runs with **the same
script** on a local machine and in CI (`.github/workflows/linux.yml`): the CI steps only call the `Tests/*.sh`
scripts. Numbers and measured results: [Quality.md](Quality.md).

## What to run when

| Change | Run | Time (Debug, 4 vCPU) |
|---|---|---|
| Any change (before a commit) | `cmake --build build && ./build/bin/ZaminTests` (ends with `N tests, 0 failures`) | ~5 minutes (~85 % of it the slice playthroughs) |
| A quick check (while working) | `./build/bin/ZaminTests --quick` (or `ctest --test-dir build -L unit`): without the `SLOW_TEST`s (whole-game playthroughs) | ~45 s |
| One part only | `./build/bin/ZaminTests "Prefab*"`, `--filter "Scene*:-*Fuzz*"`, `--list` | seconds |
| Rendering, shaders, post effects | `Tests/render_tests.sh` | seconds |
| Editor UI (panel, menu, popup, inspector) | `Tests/editor_smoke.sh` | ~3 minutes |
| Script system, module loading | `Tests/hot_reload_test.sh` | ~20 s |
| Export, player, launch script | `Tests/export_test.sh` (one game), `Tests/package_games.sh` (all three games + packages) | 1-5 minutes |
| A sample game (script, scene, generator) or the part of the engine it uses | `Tests/samples_test.py --game <Game>` (all: no arguments), `Tests/samples_assets.py --regen` | 1-5 minutes per game, ~30 minutes for all; ~20 s |
| Memory, pointers, ownership | `Tests/sanitize.sh address --smoke` (`--editor-smoke`), `Tests/sanitize.sh undefined` | 20-40 minutes (with the build) |
| A change that affects speed | `Tests/bench.sh --compare`, `Tests/render_bench.sh`, `Tests/player_bench.sh` | 1-5 minutes (Release) |
| New code: are there enough tests | `Tests/coverage.sh` (`--gui`) | ~25 minutes |
| Formatting | `Tests/format_check.sh` (changed lines) | seconds |
| Documentation, a component, the script API or a player option | `Tests/docs_check.py` (after a component change first `Tests/docs_components.py --update docs/Components.md`), `Tests/docs_snippets.py` | ~1 s, ~5 s |
| Everything (with the GUI tests) | `ctest --test-dir build` (`-LE gui` - the quick ones, `-L gui` - editor/export) | ~10 minutes |

Without a display (SSH, CI) the GUI tests use `xvfb-run` themselves (`sudo apt-get install xvfb`).

## 1. Unit tests - `ZaminTests`

`Tests/ZaminTests.cpp` (+ `Tests/TestFramework.h/.cpp`): **631 tests**, running without a GPU or a window
(headless) - an ordinary program linked with the engine library (`libZamin.so`). Scenes, the ECS, serialization,
prefabs, scripts and their fields, 2D/3D physics, animation, particles, UI, audio, navigation and AI, project
settings, player settings, editor logic (the parts without panels), math and the helper libraries. The full list is
below, in the [appendix](#appendix-all-unit-tests).

```bash
./build/bin/ZaminTests                         # everything; the last line: "628 tests, 0 failures"
./build/bin/ZaminTests Prefab*                 # a glob (case-insensitive); without *, ? - a part of the name
./build/bin/ZaminTests --filter "Scene*:-*Fuzz*"   # several patterns (: or ,), '-' excludes
./build/bin/ZaminTests Prefab* -Physics*       # the same with separate arguments (a single letter like "-x" is an option)
./build/bin/ZaminTests --list                  # the names only
./build/bin/ZaminTests --quick                 # without the SLOW_TESTs (whole slice playthroughs): a quick check; --slow - only those
./build/bin/ZaminTests --timing --slowest 20   # the timing report: total, mean, median, the slowest, the distribution
./build/bin/ZaminTests --report r.json --junit r.xml --fail-fast
ZAMIN_TESTS=Physics ./build/bin/ZaminTests     # a filter through the environment variable (the old way)
```

Writing a new test:

```cpp
TEST(MyFeatureWorks)
{
    Ref<Scene> scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("A");
    CHECK(e.HasComponent<TransformComponent>());
    CHECK_NEAR(e.GetTransform().Scale.x, 1.0f, 1e-6f);
}
```

- The tests have no GPU: do not create textures, meshes, fonts or shaders. A scene without a primary camera
  (`Primary`) is not drawn, so `OnUpdateRuntime` (`UpdateRuntime` + `RenderRuntime`) is safe headless. The edit-mode
  frame `OnUpdateEditor` draws from the editor camera; tests use its non-drawing half, `UpdateEditorAnimation`.
- A failed `CHECK` fails the test and the other tests keep running; an exception that escapes a test fails it too.
- Random numbers use a fixed seed; timing-dependent checks (speed-ups, linear growth) have a safety margin and are
  skipped in a coverage build (`ZAMIN_COVERAGE_BUILD`).
- Tests that write files use their own folder under `std::filesystem::temp_directory_path()` and delete it.
- Some tests read the sample projects (`Projects/*`, `ZAMIN_SOURCE_DIR`): the samples' assets have to stay valid.
- After adding a test, update the list: `Tests/test_catalog.py --update docs/Testing.md`.
- A test that plays a whole game (tens of seconds: the slices' oracle, defeat, determinism and resume tests) is
  written with `SLOW_TEST`, not `TEST`: a full run and CI run them, `--quick` leaves them out, `--slow` runs only
  them.

`ctest` runs two entries: `ZaminTests` (`--quick --timing --report build/TestResults/ZaminTests.json --junit
build/TestResults/ZaminTests.xml`, label `unit`) and `ZaminSliceTests` (`--slow`, `ZaminSliceTests.json/.xml`, label
`slices;slow`) — `ctest -j2` runs them side by side, `-LE gui` takes both; CI uploads these files as the
`unit-tests-<compiler>` artifact.

## 2. Rendering regression test - `ZaminRenderTests`

`Tests/RenderTests.cpp`: reference scenes are drawn offscreen at 480x270 and compared with `Tests/Reference/*.png`.
A case fails when more than `--max-percent` (0.5 %) of the pixels differ by more than `--threshold` (8) in a
channel; then the new picture, the difference picture (`<name>.diff.png`) and `<name>.compare.png` (reference | new |
difference) are written to `build/RenderTests/`; every run writes `report.json` and `summary.md`.

| Case | What it guards |
|---|---|
| `Lit3D` | PBR primitives, the sun's shadow (cascades), point and spot lights, the gradient sky |
| `Lit3D_NoCulling` | frustum culling and instancing off - has to come out the same as `Lit3D` |
| `Materials` | transparent (sorted), emissive + bloom, unlit, double-sided, an instanced row, fog |
| `Sky` | several suns (only one casts a shadow), the curvature of the sky gradient, a large sun disc |
| `PostEffects` | SSAO, bloom tint and lens dirt, chromatic aberration, vignette, film grain, FXAA |
| `DebugNormals` | the debug view: normals |
| `Sprites2D` | Renderer2D: sprites (rotated, transparent, sorted), circles and rings, text |
| `Particles` | additive circles, transparent quads, a flipbook, local space, trails (GPU instancing) |
| `Particles_NoInstancing` | instancing off - has to come out the same as `Particles` |
| `Showcase3D` | the Showcase3D sample: models, textures, HDRI, a skinned mesh |
| `Platformer2D` | the Platformer2D sample: tilemap, sprites, text |
| `SelectionOutline` | the editor's selection outline: a skinned model, an occluded entity, multi-selection, hover |

```bash
Tests/render_tests.sh                    # everything (llvmpipe, a 1280x720x24 xvfb when there is no display)
Tests/render_tests.sh --filter Sky --save-all --out /tmp/rt
Tests/render_tests.sh --update           # rewrite the references when the look changed on purpose
ZAMIN_BUILD_DIR=build-clang Tests/render_tests.sh
```

The script always uses Mesa's software rasterizer (llvmpipe) - the references were made with it. CMake targets:
`render_tests`, `update_render_references`.

## 3. Editor smoke test - all the windows

`ZAMIN_EDITOR_OPEN_ALL_PANELS=1` (`Editor/src/EditorSmokeTest.cpp`), script: `Tests/editor_smoke.sh`. On a copy of a
sample project the editor opens, for a few frames each, every window (alone and all together), every settings page,
every menu and submenu, the viewport popups, the prompt dialogs, the inspector of every asset type, every entity of
the scene and the inspector of every component added in turn to a temporary entity, the sprite editor, prefab mode,
the built-in layouts, and Play / Pause / Stop / Simulate (Showcase3D - 221 steps, Platformer2D - 263 steps). An error
in the log (OpenGL debug output too, `ZAMIN_GL_DEBUG=1`), the OpenGL error flag, a failed ImGui check (`IM_ASSERT` is
on in every build) or a run that does not reach the last step fails the test (exit code 1).

```bash
Tests/editor_smoke.sh                        # Showcase3D and Platformer2D
Tests/editor_smoke.sh Projects/Adventure3D --shots --out /tmp/smoke
```

## 4. Script hot reload test

`Tests/hot_reload_test.sh` (`Tests/Fixtures/HotReload`, hook `ZAMIN_EDITOR_HOT_RELOAD_TEST`): the editor builds the
missing module itself and plays version 1; the test changes the source and rebuilds the module outside while the
editor is running; the editor loads the new module and plays version 2. Checked: the field values of the scene are
kept (`Speed=7.5`, `Label`, `Count`), a new field gets its default value (`Extra=42`), the new code runs, the log has
no error, the scene file is unchanged. The in-process variant is the `ScriptReloadKeepsFieldValues` unit test.

## 5. Export test and game builds

`Tests/export_test.sh` exports a sample game with the editor (`ZAMIN_EDITOR_EXPORT`, xvfb) and checks it in the state
a player receives it: `export-report.json` reports success; `<Game>.sh --version`; the exported game runs N frames
with `--benchmark` (exit code 0, no error in the log, the script module loaded); once more with a headless screenshot
(`Tests/png_check.py`: 1280x720, not a single color); it still starts after being moved to another folder and the
`.desktop` icon points to the new place.

`Tests/package_games.sh` does this for all three sample games (Platformer2D, Showcase3D, Adventure3D) and packages
only an export that passed every check as a **game build**: `<Name>-<version>-linux-x86_64.tar.gz` + `.sha256`,
`packages.md`, `summary.md`. The package is unpacked again and the launch script is called from there (tar.gz keeps
the files' executable bits; the zip of a CI artifact used to lose them). CI uploads these packages as the
`player-builds-<compiler>` artifact (kept for 14 days).

```bash
Tests/export_test.sh                                   # Platformer2D
Tests/export_test.sh --project Projects/Adventure3D --frames 60 --keep
Tests/package_games.sh --out dist                      # all three games -> dist/*.tar.gz
```

## 5a. Sample games test

`Tests/samples_test.py` runs all nine samples headless in `ZaminPlayer` (xvfb when there is no display) and checks
them by their logs:

- **demo**: the six M24 games start with `ZAMIN_DEMO=1` and a bot plays them. The `Demo: ...` lines of the log have
  to show a result:
  - Neon Arena: the score, killed enemies, wave 2, a combo;
  - Breakout: level 1 is finished and level 2 starts (the `level6` case: the last level with `ZAMIN_START_LEVEL=6`);
  - FPS Arena: a parkour lap, checkpoints, targets hit;
  - Warehouse Keeper: at least 3 solved levels and an undo;
  - Silk Road: a distance of at least 400 m, the bot hits nothing;
  - Valley Race: the first lap is finished.
- **menus**: no bot, `ZAMIN_PLAYER_INPUT` presses the keys. Enter in the main menu ("Play" is selected), then Esc —
  pause, Esc again — the game continues, then "Main menu" from the pause menu. In the Warehouse Keeper and Silk Road
  cases the results panel opens too. Warehouse Keeper's `menuwait` case waits 10 s in the main menu (the bot behind
  it moves on to the next level), then checks that "Play" opens the player's level 1 and that a second step pressed
  quickly during the step animation is not lost. The games write `Menu: main menu` / `pause` / `pause closed` /
  `results` to the log for each panel, and the test checks the order of these lines. In Platformer2D the game goes
  from `Menu.zscene` through the loading screen to level 1.
- **smoke**: the three older samples (Platformer2D, Showcase3D, Adventure3D) start and their scene loads.
- **record** (after every `demo`): the demo plays with `ZAMIN_DEMO_RECORDS=1`, so the bot's result becomes a record
  too. The log has to contain `New record: ...` and the game's SaveData file (`savedata.json`, in the run's `HOME`)
  has to keep the record keys. Then a short second run with the same `HOME` (without the bot) reads the same value
  in its `Records: ...` line and opens the main menu (`--no-records` — skip this).
- **gamepad**: the six games with a simulated gamepad only (`ZAMIN_PLAYER_INPUT`: `pad=A`, `padhold=` /
  `padrelease=`, `stick=LeftX,0.8`). A in the main menu, the game's own controls (sticks, buttons, triggers), Start —
  pause and back, D-pad down + A — the main menu. Checked: the hints switch to the gamepad (`Input device: gamepad`,
  without going back to the keyboard) and the game shows a result: in Neon Arena a shot fired with RT, in Breakout a
  broken brick, in Warehouse Keeper level 1 solved with the D-pad, in Silk Road the distance — written as a record.

Every run also checks the following:

- exit code 0;
- no `ERROR` / `CRITICAL` line in the log;
- the script module loaded;
- the picture of the last frame is not a single color (`Tests/png_check.py`).

The game clock is fixed: with `ZAMIN_CAPTURE_FPS=20` (`--capture-fps`) every frame is exactly 1/20 s
(`Time::SetCaptureFrameRate`), so N frames are the same game time on every machine. The bots play with seeded random
numbers, so the result is the same on a slow and on a fast machine. A human game picks a fresh seed every session
(`RandomGenerator::Randomize`: new enemies every time); the test sets `ZAMIN_RANDOM_SEED=1` (`--random-seed`), so
those fresh seeds are the same on every run too and the menu and gamepad cases see the same game each time. Every run gets a fresh `HOME` (no saved records
or settings). The report is written to `report.json`; the logs and pictures stay in the `--out` folder.

`Tests/samples_assets.py` (no display, ~1 s) checks the procedural assets of the six M24 games:

- `Assets/` is no larger than 2 MiB;
- every asset has a `.zmeta`, no UUID repeats;
- the PNG and WAV files are correct: 16-bit PCM, not silent, not clipped;
- every file named by a scene, the project file or the scripts exists, every sound and texture is used somewhere.

With `--regen` every generator (`Tools/samples/gen_*.py`) is run into a temporary folder (`ZAMIN_SAMPLES_OUT`) and
the result is compared byte for byte with the files of the repository. This proves that the assets really come from
the generator and can be regenerated (needs numpy).

```bash
Tests/samples_test.py                              # everything (~20 minutes, llvmpipe)
Tests/samples_test.py --game Racing --kind menus   # one case of one game
Tests/samples_test.py --list                       # the cases and checks
Tests/samples_test.py --out build/SampleTests --frames-scale 2
Tests/samples_assets.py --regen --verbose          # the asset table + the comparison with the generators
Tests/samples_export.py --game Breakout            # export, then check by playing the exported game
ZAMIN_BUILD_DIR=build-release Tests/samples_bench.py   # performance (Release, llvmpipe)
```

`Tests/samples_assets.py` checks the input map as well: every action has a keyboard / mouse and a gamepad binding,
every action / axis / vector the scripts read is in the map and every one in the map is read, the scripts never
query the keyboard or the gamepad directly, and the control hint of the main menu uses only names from the map.

`Tests/samples_export.py` (ctest `SampleExports`) exports the nine samples with the editor (`Tests/export_test.sh`)
and plays the exported games (`<export>/<Game>.sh`) with `samples_test.py --exported`: the demo + record cases of the
six games and the smoke of the older samples. `Tests/samples_bench.py` measures each sample's frame time, the number
of entities in the scene and its most expensive parts ([Samples.md](Samples.md#performance)).

## 6. Sanitizers

`Tests/sanitize.sh <address|undefined|leak|thread>` builds with a sanitizer in a separate folder (`build-asan`,
`build-ubsan`, ...; `-DZAMIN_SANITIZE=...`, `cmake/Sanitizers.cmake`) and runs `ZaminTests`; it fails on any
sanitizer report even when the tests pass. The noise of third-party code is in `Tests/sanitizers/*.supp`, with
comments.

```bash
Tests/sanitize.sh address --smoke --editor-smoke   # ASan + LSan: the tests, 3 games, the editor, the editor smoke test
Tests/sanitize.sh undefined --compiler clang       # UBSan (every report in our code is fatal: -fno-sanitize-recover=all), Clang
Tests/sanitize.sh leak                             # LeakSanitizer alone
Tests/sanitize.sh address --filter Prefab --clean  # some tests only; the build folder is deleted at the end
```

`<log folder>/summary.md` lists every run (result, time, log). In CI the `sanitizers` job: `asan`
(`--smoke --editor-smoke`) and `ubsan-clang`. With Clang the sanitizer runtimes are a separate package:
`sudo apt-get install libclang-rt-18-dev` (the script says when it is missing).

When the implementation of third-party code is compiled inside one of our targets (miniaudio for example -
`Zamin/src/Zamin/Audio/MiniaudioImpl.cpp`), it lives in its own translation unit and is recoverable in a sanitizer
build like a vendor library (`-fsanitize-recover=all`, `Zamin/CMakeLists.txt`) - so its report is suppressed in
`ubsan.supp` with a comment, while every report of the engine's code stays fatal.

## 7. Benchmarks as regression tests

| Command | What | Limit |
|---|---|---|
| `ctest --test-dir build -L bench` | `BenchEcsSmoke`, `BenchSerializeSmoke`, `BenchCombatSmoke`, `BenchPhysicsSmoke`, `BenchAnimationSmoke`, `BenchSliceStressSmoke`: at a small size; exit code 3 when an invariant of the stress benchmarks breaks (tunnelling, NaN, a foot under the terrain, determinism, blend weight, events at every rate, an allocation-free frame, a graph error) | exit code |
| `Tests/bench.sh --compare` | the ECS, serialization, prefab, combat, physics, animation and slice stress benchmarks; compared with `Tests/Baselines/*-release.json` (`Tests/bench_compare.py`) | a noticeable slowdown |
| `python3 Tests/slice_bench.py --build build-release --check --compare Tests/Baselines/slices-release.json` | the vertical slice scenes (Menu, Orchards, Crypt, Pass) in ZaminPlayer `--benchmark`, played by the demo pilot: 284x160 Low (the game code gate) and 1280x720 (compared with the baseline); `--soak 18000` memory growth in the attract loop ([VerticalSlices.md](VerticalSlices.md#resume-export-and-profiling)) | frame ≤ 16.6 ms (2D) / 33.3 ms (3D, llvmpipe), game code ≤ 3 / 4 ms (p95 ≤ 6), +15 %, RAM growth ≤ 32 MiB |
| `python3 Tests/slice_ui_test.py` | the UI gate of the vertical slices in the Player: keyboard / gamepad / mouse runs, UI scale 80–120 % and its being applied again in a new process, the audit at 6 sizes (`ZAMIN_UI_AUDIT`), seeded fuzz, the 2560x1080 menu ([VerticalSlices.md](VerticalSlices.md#gates)) | the audit rules, invariants |
| `Tests/render_bench.sh --max-avg 200` | 10 000 sprites, 1 000 meshes (with / without instancing), both | ms / frame |
| `Tests/player_bench.sh --max-avg 50 --max-p95 80` | the start scenes of the sample games in ZaminPlayer | ms / frame |
| `Tests/startup_bench.py` | editor and player startup time (to the first frame, with its parts), comparing two builds | ms (median) |

Baseline results: [Benchmarks.md](Benchmarks.md).

## 8. Code coverage

`Tests/coverage.sh [--gui] [--html]` (`-DZAMIN_COVERAGE=ON`, `build-cov`), the report by module
(`Tests/coverage_report.py`): [Coverage.md](Coverage.md).

## 9. Formatting - `clang-format`

`.clang-format` describes the existing code style (4 spaces, braces on a new line, the inside of `namespace Zamin {`
indented, no column limit - lines broken by hand are kept). The code base is not reformatted as a whole:
`Tests/format_check.sh` checks only the changed lines (`git clang-format`).

```bash
Tests/format_check.sh                     # uncommitted changes (relative to HEAD)
Tests/format_check.sh --base main         # since the branch left main
Tests/format_check.sh --fix --base main   # format those lines in place
Tests/format_check.sh --files A.cpp       # a whole file
Tests/format_check.sh --stats --top 20    # how far the whole code base is from .clang-format (Tests/format_stats.py)
```

The `format` job of CI is advisory (an annotation per hunk, the diff on the job page) and guards `.clang-format`
itself: the lines that would change in the whole code base must not exceed 3.5 % (`--stats --max-percent 3.5`).

## 10. Compile-time checks

- **Warnings:** our targets (engine, editor, player, tests and benchmarks, the sample game modules) are built with
  `-Wall -Wextra -Wpedantic` (`zamin_target_settings`, `cmake/ZaminGame.cmake`), vendor headers are included through
  `-isystem`; the build has to be free of warnings with GCC and Clang. `-ffp-contract=off`: GCC and Clang builds give
  the same float results (no FMA contraction — Jolt passes `-mfma` to every target linked with it; Clang used to
  contract).
- **Script templates:** `ZaminScriptTemplateGen` generates the editor's "New script" templates and they are compiled
  together with the engine (`ZaminScriptTemplateCheck`) - a broken template fails the build.
- **Shipping DebugDraw:** `Tests/DebugDrawShippingCheck.cpp` compiles the DebugDraw API with `ZM_DEBUG_DRAW 0` (the
  empty functions of a Shipping build) and `ZaminTests` calls it.
- **The sample games** (`Projects/*`) are part of the main build: when the engine API changes they are compiled too.
- **Documentation** (`Tests/docs_check.py`, ctest: `DocsCheck`): every relative link and image of the Markdown files
  exists (`#section` links — against GitHub's heading anchors), every component of `AllComponents` is documented in
  [Components.md](Components.md) and its tables generated from the source are current
  (`Tests/docs_components.py`), every public function of `ScriptableEntity` is mentioned in
  [Scripting.md](Scripting.md), every option of the player's `--help` is in [Runtime.md](Runtime.md) or
  [BuildAndExport.md](BuildAndExport.md), every log / UI message quoted in [FAQ.md](FAQ.md) (a line of the form
  `` > `...` ``; `<...>` - a formatted value) is written exactly so in the source code, every `ZAMIN_*` CMake option
  is documented in [BuildAndExport.md](BuildAndExport.md) and `PROJECT_NUMBER` of the `Doxyfile` equals the engine
  version. Also: the unit test catalogue at the end of this document is current (`Tests/test_catalog.py`),
  [CHANGELOG.md](../CHANGELOG.md) matches the git history — every milestone that is in the history and has a
  finished task has a section, the line under the section (date, tasks, the number and range of commits) is the same
  as the one `Tests/changelog.py` prints, every commit mentioned exists —, the [documentation index](README.md)
  links to every document of `docs/` and to `CONTRIBUTING.md` and `CHANGELOG.md`, and every image of `docs/images/`
  is used in some document and is no larger than 400 KB and 1280 px wide.
- **Code in the documentation** (`Tests/docs_snippets.py`, ctest: `DocsSnippets`): the whole script classes of
  `README.md` and `docs/*.md` (```` ```cpp ```` blocks with `class X : public ScriptableEntity` ... `};`) are
  compiled with `-fsyntax-only`, with the same compiler and flags as the sample games' scripts
  (`build/compile_commands.json`); a warning counts as an error and the message points to the Markdown file and
  line. Code fragments (not a whole class) are not checked.

## ctest

| Test | Label | What |
|---|---|---|
| `ZaminTests` | - | the unit tests (timing report, JSON and JUnit results in `build/TestResults`) |
| `DocsCheck` | `docs` | `Tests/docs_check.py` - the documentation agrees with the code (section 10) |
| `DocsSnippets` | `docs` | `Tests/docs_snippets.py` - the script classes of the documentation compile |
| `BenchEcsSmoke`, `BenchSerializeSmoke`, `BenchCombatSmoke`, `BenchPhysicsSmoke`, `BenchAnimationSmoke`, `BenchSliceStressSmoke` | `bench` | the benchmarks at a small size (with the invariants of the stress benchmarks) |
| `EditorSmoke` | `gui;slow` | `Tests/editor_smoke.sh` |
| `ScriptHotReload` | `gui;slow` | `Tests/hot_reload_test.sh` |
| `ExportTest` | `gui;slow` | `Tests/export_test.sh` (Platformer2D) |
| `SampleAssets` | `samples` | `Tests/samples_assets.py` - the procedural assets of the sample games |
| `Sample<Game>` (9 of them: `SampleTopDownShooter` ... `SampleAdventure3D`) | `samples;gui;slow` | `Tests/samples_test.py --game <Game>` (logs and pictures in `build/SampleTests/<Game>`) |
| `SampleExports` | `samples;gui;slow` | `Tests/samples_export.py` - every sample is exported and the exported game is played headless (`build/SampleExports`) |

`ctest --test-dir build -LE gui` - the quick ones (the "Unit tests" step of CI), `ctest --test-dir build -L gui` -
the GUI tests (sequential, need a display or xvfb), `ctest --test-dir build -L samples` - the sample games (or
`cmake --build build --target samples_test`).

## CI (`.github/workflows/linux.yml`)

| Job / step | Script | Artifact |
|---|---|---|
| `build` (gcc, clang; Release): Unit tests | `ctest -LE gui` | `unit-tests-<cc>` |
| `build`: Rendering regression test | `Tests/render_tests.sh --save-all` | `render-tests-<cc>` |
| `build`: Editor smoke test | `Tests/editor_smoke.sh --shots` | `editor-smoke-<cc>` |
| `build`: Script hot reload test | `Tests/hot_reload_test.sh` | - |
| `build`: Export test + game builds | `Tests/package_games.sh --out player-builds` | `export-test-<cc>`, `player-builds-<cc>` |
| `build`: headless screenshots | `ZaminPlayer` / `ZaminEditor` (`ZAMIN_MAX_FRAMES`, `ZAMIN_SCREENSHOT`) | `screenshots-<cc>` |
| `sanitizers` (`asan`, `ubsan-clang`) | `Tests/sanitize.sh address --smoke --editor-smoke`, `Tests/sanitize.sh undefined --compiler clang` | `sanitizer-<name>` |
| `format` | `Tests/format_check.sh --base <previous commit> --github`, `--stats --max-percent 3.5` | - |
| `coverage` | `Tests/coverage.sh --html` | `coverage-report` |

## Tests that guard found bugs

Each of them keeps a bug that was found once (often with the tools of this very milestone) and fixed from coming
back: with the fix removed the test fails.

| Test | Bug | How it was found |
|---|---|---|
| `AudioMixerGroupTeardown` | when a mixer group with several child groups was deleted, miniaudio detached only the first input node and the others read freed memory (a crash in `AudioEngine::Shutdown`) | AddressSanitizer |
| `PhysicsDeterminism2D`, `PhysicsDeterminism3D` | Box2D / Jolt bodies were created in component pool order: a scene played in the editor and the same scene loaded from a file (exported) simulated differently | the determinism test (bit-level comparison) |
| `PlayCopyRunsScriptsInFileOrder`, `PhysicsDeterminism2D/3D` (as in the game, without a copy) | `Scene::Copy` copied the component pools through a view (newest first): on Play in the editor the scripts (`OnCreate` ... `OnLateUpdate`), animators and agents ran in the reverse of the exported game's order | review (the tests played a scene read from a file through a copy too) |
| `DeferredDestroyOnceEachAndLinear` | `DestroyEntityDeferred` searched the whole queue for a duplicate: thousands of `Destroy` calls in a frame were quadratic (10 000 - 525 ms) | the ECS benchmark |
| `ScriptReloadKeepsFieldValues` | the script field values of a scene survive a hot reload | the hot reload test |
| `StaleModuleCopiesRemoved` | the script module copies of crashed / killed processes piled up in `/tmp/zamin-modules` (423 MB) | the hot reload test |
| `ParticleSimulationUnchanged` | GCC and Clang took the random numbers of an emitter in a different order (several calls in one expression) | the Clang Release build |
| `Kinematic3DFollowsTransformAtLowFPS` | a kinematic body's velocity was wrong with several physics steps per frame | regression |
| `DestroyKeepsOtherScriptsAlive` | EnTT moves the last component into the place of a removed one - another entity's script was lost | regression |
| `DestroyEntityReentrant` | destroying from inside `OnDestroy` worked with a stale pointer | regression |
| `EntitySetPartialLoad`, `SceneSalvage` | one broken record lost the whole scene; a file cut off in the middle of a write | broken files |
| `SerializationFuzz` | random / broken JSON (infinite floats - `Tests/Fixtures/Fuzz`) does not crash the loader | the fuzz test |
| `SceneSaveDeterministic`, `SceneCopyIsIndependent` | the scene file is the same on every save; the Play copy does not affect the original scene | regression |
| `MemoryStatsProcess` | Clang optimized away the memory allocated by the test (a volatile write) | the Clang Release build |
| `CrashHandlerReports` | the backtrace guessed frame 0 - wrong in a sanitizer build | the sanitizer build |

Bugs the GUI tests found as well: the editor loaded a module being built outside while the linker was still writing
it (now it waits until the time and size stop changing for one poll - the hot reload test); the terminal log was
fully buffered into a pipe and lost on a crash (now line by line); the editor smoke test did not see the Inspector
tab (found by code coverage).

## Appendix: all unit tests

The section name is the section heading in `Tests/ZaminTests.cpp` (a code comment, in English).

<!-- test-catalog:begin -->
_Generated by `Tests/test_catalog.py --update docs/Testing.md`: 1370 unit tests, 143 sections (in the order of the files of the `ZaminTests` target). To run one test: `./build/bin/ZaminTests <Name>`._

| # | Section | Tests | Names |
|---|---|---|---|
| 1 | SliceUILayoutTests.cpp | 6 | `UIAuditRulesNameEachLayoutDefect`, `UIAuditLaysOutAtAnyViewSizeAndKeepsTheScenesOwn`, `SliceMenuLayoutLanternKeeper`, `SliceMenuLayoutBeaconPass`, `SliceMenuLayoutCatchesABrokenLinkAndTinyText`, `SliceMenuSettingsKeptOnEveryCloseAndLanguagePersists` |
| 2 | LoadoutModelTests.cpp | 6 | `LoadoutModelValidatesStatsAndDefinitions`, `LoadoutModelEquipmentMovesOwnershipWithoutDuplication`, `LoadoutModelFullBagUnequipAndSwapRollback`, `LoadoutModelClampBonusesAndEnergyOnRemoval`, `LoadoutModelTickRegenerationCooldownAndInvalidDelta`, `LoadoutModelRestoreRejectsDefinitionAndProgressMismatch` |
| 3 | LoadoutWorldTests.cpp | 20 | `LoadoutWorldEquipmentAppliesToRealCombatAndArmor`, `LoadoutWorldAbilityChargesOnceAndCooldownDoesNotUseAttackClock`, `LoadoutWorldSpellPowerAndAttackCompose`, `LoadoutWorldBlockedOutOfRangeAndFriendlyCastPreserveResources`, `LoadoutWorldMissingIngredientAndLockedAbilityAreTransactional`, `LoadoutWorldInsufficientEnergyAndRefillClamp`, `LoadoutWorldHealShieldNoEffectDoesNotConsumePotion`, `LoadoutWorldInvulnerabilityDoesNotChargeCast`, `LoadoutWorldDeadInactiveForeignAndGenerationIsolation`, `LoadoutWorldPauseAndInactiveFreezeEnergyAndCooldown`, `LoadoutWorldVersion3SaveRestoresEquipmentResourcesAndCooldown`, `LoadoutWorldMalformedSaveRejectsWholeTransaction`, `LoadoutWorldLegacyV1AndV2MigrationInitializesAuthoredState`, `LoadoutWorldChangedDefinitionRejectedBeforeMutation`, `LoadoutWorldFreshSceneStableKeyRestore`, `LoadoutWorldDefinitionsAndEventsAreBounded`, `LoadoutWorldQuestKillIntegrationAndEquipmentDoesNotForgeCollect`, `LoadoutWorldBlueprintCatalogDispatchAndTypedValidation`, `LoadoutWorldRuntimeStopStartClearsOwnedRecords`, `LoadoutWorldWorkshopGraphsValidateAndRoundtrip` |
| 4 | ProjectileModelTests.cpp | 12 | `ProjectileModelSpecValidationBounds`, `ProjectileModelSweptSphereCatchesFastThinTargetAt20Hz`, `ProjectileModelStartInsideIsZeroAndDegenerateSegmentIsPointTest`, `ProjectileModel2DIgnoresDepth`, `ProjectileModelSweepOrdersByTThenIdAndTruncates`, `ProjectileModelSweepSkipsOwnerHitAndTeam`, `ProjectileModelAdmissionMatrix`, `ProjectileModelHomingTurnCapAndSpeedPreserved`, `ProjectileModelGravityArcIsSemiImplicitEuler`, `ProjectileModelMuzzleSegmentStartsAtAimPoint`, `ProjectileModelPoolLowestFreeGenerationsAndStaleHandles`, `ProjectileModelStateValidationForRestore` |
| 5 | EffectModelTests.cpp | 11 | `EffectModelSpecValidationBounds`, `EffectModelRefreshStacksResetTimerAndKeepTickPhase`, `EffectModelExtendAddsDurationUpToCap`, `EffectModelIndependentEvictsLeastRemainingThenOldest`, `EffectModelHighestWinsStrongerReplacesEqualExtendsWeakerRejected`, `EffectModelBookCapacityRejectsWithoutEvictingOtherKeys`, `EffectModelExactTickCountsAcrossStepRates`, `EffectModelTickCapCarriesAndTicksBeforeExpiry`, `EffectModelComposeModifiersSlowHasteStunAndStacks`, `EffectModelRemoveAndValidateBook`, `AreaModelValidationFalloffAndOrdering` |
| 6 | CombatWorldTests.cpp | 20 | `CombatWorldProjectileHitsThroughSharedRulesWithOwnershipEvents`, `CombatWorldAttackIsSnapshottedAtLaunchAndArmorAppliesAtImpact`, `CombatWorldOwnerlessProjectileKeepsSnapshotsAndExcludesFormerOwner`, `CombatWorldPierceHitsInSegmentOrderOncePerTargetThenSpends`, `CombatWorldRejectedHitConsumesShotAndDeadVictimIsPassedThrough`, `CombatWorldBox2DWallBlocksAndMuzzleInsideWallImpacts`, `CombatWorldWallFaceOnAStepBoundaryStillBlocks`, `CombatWorldHealthActorCollidersAreActorChannelOnly`, `CombatWorldJoltWallBlocksAndCharacterIsHitOnceThroughActorChannel`, `CombatWorldBudgetsPerFrameOwnerAndPoolAreExplicit`, `CombatWorldLifetimeAndDistanceRoundUpToFixedSteps`, `CombatWorldPauseAndTimeScaleZeroFreezeProjectiles`, `CombatWorldPlayStopStartResetsDefinitionsAndProjectiles`, `CombatWorldDefinitionsAreSharedImmutableAndBounded`, `CombatWorldVisualPrefabPoolingValidationAndBudget`, `CombatWorldSaveRestoreReplacesProjectilesInFlight`, `CombatWorldBlueprintCatalogDispatchAndTypedValidation`, `CombatWorldDeterministicEventLogHashAcrossFreshScenes`, `CombatWorldFrameRateIndependentOutcomes`, `CombatRulesDamageRequestGraceTeamCauseAndArmorOnly` |
| 7 | CombatEffectTests.cpp | 22 | `CombatEffectDefinitionsAreSharedValidatedAndResolvedAtUse`, `CombatEffectStackingRulesThroughTheWorldEmitEvents`, `CombatEffectDamageOverTimeTicksIgnoreGraceAndCreditKills`, `CombatEffectDeathClearsEffectsAndRespawnStartsClean`, `CombatEffectInactiveActorsPauseAndTimeScaleFreezeEffects`, `CombatEffectAdmissionTeamsSourcesBudgetAndLimits`, `CombatEffectStatBonusComposesWithLoadoutAndClampsEnergy`, `CombatEffectStunGatesActionsWithoutSpendingAnything`, `CombatEffectSlowHasteAndStunScaleMotors`, `CombatEffectSlowAndStunDriveAINavigationAndFreezeTheBrain`, `CombatAreaFalloffOrderingTargetCapTeamsAndBudget`, `CombatAreaSupportHealsAlliesShieldsAndAffectsOwnerOnRequest`, `CombatAreaLineOfSightUsesBox2DAndJoltWalls`, `CombatProjectileImpactDetonatesAreaWithLaunchSnapshotAndEffects`, `CombatProjectileDetonatesOnExpiryCancelAndWallImpact`, `CombatDetonationBudgetDefersTheRestToLaterSteps`, `CombatEffectBlueprintNodesDispatchAndValidate`, `CombatEffectDeterministicEventLogAcrossFreshScenesAndFrameRates`, `CombatEffectSaveV4RestoresEffectsAndBuffedEnergy`, `CombatEffectPlayStopStartResetsEffectsAndDefinitions`, `CombatImpactDetonationLineOfSightNeverSeesThroughTheStruckWall`, `CombatPureNodesNeverCreateTheCombatWorld` |
| 8 | CombatSaveTests.cpp | 13 | `GameSaveLegacyFixturesDecodeAndReencodeIdentically`, `GameSaveLegacyV3RestoresIntoCombatScene`, `GameSaveModelV4RoundtripKeepsEffectsAndProjectilesExactly`, `GameSaveModelV4StrictFieldsAndBoundsRejectWholeSnapshot`, `GameSaveModelMaximalV4DocumentFitsTwoMiB`, `CombatSaveV4ContinuesExactlyInAFreshScene`, `CombatSaveUnresolvedSourcesAndOwnersBecomeOwnerless`, `CombatSaveEffectsAreStagedBeforeLoadoutEnergyValidation`, `CombatSaveChangedDefinitionsAndInvalidStateRejectWithoutMutation`, `CombatSaveLegacyVersionMigratesBoundActorsToNoEffectsOrProjectiles`, `CombatSaveUnlinkedAbilitySignaturesStayByteIdentical`, `GameSaveModelV4TickPhaseMayExceedItsPeriodByTheModelEpsilon`, `CombatSaveOwnerlessProjectileKeepsItsExclusionAndTeamAfterALoad` |
| 9 | CombatAuthoringTests.cpp | 10 | `CombatAbilityProjectileCommitsAtLaunchAndAMissStillSpends`, `CombatAbilityEffectLinksOverrideAndFollowInstantHits`, `CombatAbilityAreaCommitsOnlyWhenItReachesATarget`, `CombatAbilityBlueprintPinsAimAndReturnTheProjectile`, `CombatHUDEffectStacksTimeLeftAndHitMarker`, `CombatHUDFeedRowsFadeAndRestoreAuthoredText`, `GameUIModelCombatRowsRoundAndFade`, `VisualGraphPinChoicesAreTranslated`, `CombatAbilityCastKeepsEnergyWithinAnEffectLoweredCapacity`, `CombatHUDZeroTicksAreNoHitsAndTickBurstsNeverHideTheMarker` |
| 10 | CombatWorkshopTests.cpp | 2 | `CombatWorkshopGraphsValidateAndRoundtrip`, `CombatWorkshopOracleHeadless` |
| 11 | LanternKeeperTests.cpp | 7 | `LanternKeeperGraphsValidateAndScenesLoad`, `LanternKeeperOracleHeadless`, `LanternKeeperLosePlanHeadless`, `LanternKeeperDeterminism`, `LanternKeeperResumeMatrixHeadless`, `LanternKeeperShrineSaveAndLevelExitCardHoldUp`, `LanternKeeperBossFightResumesInPhaseTwo` |
| 12 | BeaconPassTests.cpp | 8 | `BeaconPassGraphsValidateAndScenesLoad`, `BeaconPassOracleHeadless`, `BeaconPassLocomotionHeadless`, `BeaconPassLosePlanHeadless`, `BeaconPassSaveResumeHeadless`, `BeaconPassDeterminism`, `BeaconPassResumeMatrixHeadless`, `BeaconPassCameraStickTurnsAtARate` |
| 13 | QuestModelTests.cpp | 12 | `QuestModelDefinitionLimitsAndUniqueObjectiveKeys`, `QuestModelStartIsExplicitAndCannotRestart`, `QuestModelManualReceiptIsDeduplicatedWithoutMutation`, `QuestModelManualCannotAdvanceAutomaticObjectives`, `QuestModelReadyRequiresAllObjectivesAndClampsCounts`, `QuestModelFractionWeightsObjectivesEqually`, `QuestModelFailureIsTerminalAndKeepsEvidence`, `QuestModelReceiptCapacityIsBoundedAndTransactional`, `QuestModelForgedModesCountsAndReceiptsAreRejected`, `QuestModelUnknownObjectiveAndInvalidAmountCannotConsumeReceipt`, `QuestModelPrerequisiteDefinitionValidationRejectsDuplicatesAndSelf`, `QuestModelLevelCurveBoundariesAndCapAreDeterministic` |
| 14 | QuestWorldTests.cpp | 27 | `QuestWorldClaimCommitsBagXPAndModeExactlyOnce`, `QuestWorldFullBagCannotPartiallyGrantMultipleRewardsOrXP`, `QuestWorldPrerequisitesRequireClaimAndRejectForwardCycles`, `QuestWorldAutomaticCollectCountsCommittedGainsNotRemovals`, `QuestWorldAutomaticKillCreditsOnlyLethalMatchingSource`, `QuestWorldInteractionNoticesDoNotDoubleCountCollectedEvents`, `QuestWorldRewardGrantsCannotRecursivelyAdvanceCollection`, `QuestWorldDeathInactiveAndForeignActorsCannotMutateProgress`, `QuestWorldUUIDReplacementCannotInheritBook`, `QuestWorldHUDUpdatesFromModelAndReleasesOriginalWidgets`, `QuestWorldHUDCannotCompeteWithGameplayUIBindings`, `QuestWorldEventCapacityAndDefinitionLimitsAreBounded`, `QuestWorldSaveV2RestoresClaimsAndRewardBagTogether`, `QuestWorldSaveReceiptsSurviveNewUUIDRebinding`, `QuestWorldV1SaveMigrationIntroducesLockedZeroXPQuests`, `QuestWorldForgedXPAndCountsCannotPartiallyRestore`, `QuestWorldChangedQuestDefinitionRejectsWholeSnapshot`, `QuestWorldPrimaryBackupKeepsQuestAndRewardConsistency`, `QuestWorldBlueprintConfigureAdvanceClaimAndStateAreTyped`, `QuestWorldPlayRestartClearsBook`, `QuestWorldCodecVersion2RoundtripAndV1RejectsQuestPayload`, `QuestWorldForgedPrerequisiteStateRejectsEntireRestore`, `QuestWorldFailedQuestCannotClaimOrUnlockDependency`, `QuestWorldActorCapacityAndExplicitReplacementAreBounded`, `QuestWorldHUDUsesFallbackForDestroyedSourceAndRejectsStaleWidgetReuse`, `QuestWorldCodecRejectsDuplicateReceiptsAndUnknownQuestFields`, `QuestWorldWorkshopGraphsValidateAndRoundtrip` |
| 15 | GameplayUIModelTests.cpp | 10 | `GameUIModelStackPoliciesRestoreAfterNestedPop`, `GameUIModelDuplicateAndInvalidPushDoNotMutateStack`, `GameUIModelStackDepthIsEightAndRemovalPreservesOrder`, `GameUIModelInventoryPagesAreSortedAndClampBothEnds`, `GameUIModelEmptyBagHasOneEmptyPage`, `GameUIModelInventoryPageReflowsWhenContentsShrink`, `GameUIModelRejectsInvalidRowsBagCapacityAndCounts`, `GameUIModelHUDFractionAndRatioUseCanonicalValues`, `GameUIModelHUDFormatsCountersWithoutScientificNotation`, `GameUIModelHUDRejectsNonfiniteNegativeAndOversizedPrefix` |
| 16 | GameplayUITests.cpp | 33 | `GameUIWorldNestedPanelsRestoreFocusAndTimeScale`, `GameUIWorldModalScopeBlocksOutsideSelectionNavigationAndMousePick`, `GameUIWorldTogglePausesBeforeGameStepAndCloseDefersResumeOneFrame`, `GameUIWorldGamepadStartBAndHeldSubmitDoNotLeakAcrossOpening`, `GameUIWorldKeyboardAndGamepadNavigateOnlyTopPanel`, `GameUIWorldNonpausedPanelBlocksGameplayInputAndCloseFrame`, `GameUIWorldExternalPauseBaselineIsPreserved`, `GameUIWorldInvalidConfigurationAndLowerCloseAreTransactional`, `GameUIWorldInventoryRowsPageAndSelectionReadCanonicalBag`, `GameUIWorldInventoryShrinkDisablesEmptyRowsAndRepairsFocus`, `GameUIWorldHUDBindingsFollowHealthShieldAndInventory`, `GameUIWorldHUDAndInventoryRestoreAuthoredWidgetsWhenRemoved`, `GameUIWorldSourceUUIDReplacementDoesNotInheritBindings`, `GameUIWorldOwnerDestructionClosesMenusAndReleasesPause`, `GameUIWorldExternalPanelDeactivationReleasesPauseAndCanReopen`, `GameUIWorldDestroyedInitialFocusFallsBackInsidePanel`, `GameUIWorldInventoryConflictsAndForeignRowsAreRejected`, `GameUIWorldBoundWidgetComponentRemovalPrunesAndRestoresOthers`, `GameUIWorldSaveRestoreUpdatesHUDBagWithoutSerializingMenus`, `GameUIWorldEventQueueAndPanelCapacityAreBounded`, `GameUIWorldPlayRestartClearsScopeAndRestoresTime`, `GameUIWorldBlueprintHUDInventoryAndStateOutputsAreTyped`, `GameUIWorldNestedOrderTracksStackAndRestoresAuthoredOrder`, `GameUIWorldPanelLosingTransformCannotRetainPauseOrFocusScope`, `GameUIWorldScriptPauseDisableAndRemovalReleaseMenus`, `GameUIWorldPauseControllerBlueprintAndUnscaledClockWork`, `GameUIWorldLayoutScalesWidgetsWithinViewportAtDifferentResolutions`, `GameUIWorldWorkshopGraphsValidateAndRoundtrip`, `GameUIWorldModalFocusRejectsTextFieldOutsideScopeImmediately`, `GameUIWorldOpeningModalCancelsBackgroundScrollDrag`, `GameUIWorldSparseInventoryPageRestoresItemFocusFromFooter`, `GameUIWorldPagingAStaleViewCannotUseAnErasedRecord`, `GameplayInputEmptyActionNamesReadNothing` |
| 17 | SaveModelTests.cpp | 14 | `GameSaveModelRoundtripPreservesHealthItemsAndAI`, `GameSaveModelEncodingIsDeterministic`, `GameSaveModelCorruptionChecksumAndTruncationAreRejectedTransactionally`, `GameSaveModelUnknownVersionAndFormatAreRejected`, `GameSaveModelDuplicateJSONKeysAreRejected`, `GameSaveModelDepthAndByteLimitsAreBounded`, `GameSaveModelRejectsUnsafeKeysAndDuplicateBindings`, `GameSaveModelEntityCountAndIdentityLimits`, `GameSaveModelAbsentTombstoneIsValidButCannotHideActorPayload`, `GameSaveModelHealthDeathConsistencyAndFiniteLimits`, `GameSaveModelInventoryRejectsOverflowInvalidItemsAndZeroStacks`, `GameSaveModelSingularTransformsAndNaNAreRejected`, `GameSaveModelAITargetMustBelongToTheSnapshot`, `GameSaveModelAIStateAndCounterRangesAreChecked` |
| 18 | SaveWorldTests.cpp | 26 | `GameSaveWorldRestoresHealthBagDoorAndAIWithoutReplayRewards`, `GameSaveWorldStableKeysRemapDifferentUUIDsAndAITargets`, `GameSaveWorldInvalidLateEntityCannotPartiallyMutateHealthPoseOrAI`, `GameSaveWorldWrongSceneSchemaAndBindingSetLeaveStateUntouched`, `GameSaveWorldRecreatedUUIDCannotInheritABindingUntilExplicitRebind`, `GameSaveWorldDestroyedSlotsCaptureTombstonesAndDeactivateRecreatedSlots`, `GameSaveWorldTimersRebaseToCurrentClockAndDeadRespawnDelaySurvives`, `GameSaveWorldRestoreClearsTransientRuleAndAIEvents`, `GameSaveWorldLocal2DBodyTransformAndVelocitiesMatchRestoredPose`, `GameSaveWorldThreeDCharacterRestoreClearsControllerIntent`, `GameSaveWorldPhysicalScaleChangesAndParentChangesAreRejected`, `GameSaveWorldBindingRejectsChildrenDuplicatesForeignSceneAndUnsafeKeys`, `GameSaveWorldBindingAndIOBudgetsAreSceneSharedAndBounded`, `GameSaveWorldBackupRecoversCorruptedPrimaryAndPreservesLastGoodProgress`, `GameSaveWorldCorruptedPrimaryIsNeverPromotedOverValidBackup`, `GameSaveWorldBackupWriteFailureKeepsPrimaryUntouched`, `GameSaveWorldBothInvalidFilesAndOversizedFilesDoNotMutateState`, `GameSaveWorldChangedConfigurationDoesNotSilentlyFallBackToOldBackup`, `GameSaveWorldPlayRestartClearsBindings`, `GameSaveWorldBlueprintSaveBindFindInspectLoadOutputsAreTyped`, `GameSaveWorldManagedPrefabSlotRebindsARecreatedInstanceBeforeRestore`, `GameSaveWorldManagedSpawnRejectsTraversalAndEnforcesEightPerFrame`, `GameSaveWorldWorkshopGraphsValidateAndRoundtrip`, `GameSaveWorldForgedHealthAndInteractionProgressAreRejectedBeforeCommit`, `GameSaveWorldSlotsRejectPathEscapeAndMissingPrimaryDoesNotMutate`, `GameSaveWorldAIHomeAndPatrolMemoryRestoreAfterRehome` |
| 19 | TransferTests.cpp | 4 | `GameTransferCodecRoundtripsExactlyAndRejectsCorruption`, `SaveTransferCarriesProgressionAcrossScenesAndNeverReplaysRewards`, `SaveTransferRejectsChangedContentDeadActorsUnboundKeysAndForgedStateTransactionally`, `SaveTransferBackupRecoveryAndSceneIOBudget` |
| 20 | SliceEngineTests.cpp | 6 | `VisualGraphVectorMathNodes`, `GameplaySteerTwoDThreeDAndCameraRoundtripThroughTheMotorMapping`, `GameUIModelEnergyAbilityLevelFormatting`, `GameUIHUDKinds789FollowEnergyCooldownsUnlocksAndLevels`, `VisualGraphAppNodesFadeSettingsAndUIValues`, `VisualGraphSettingsNodesSaveTheLanguageAndDisplayChoice` |
| 21 | BrainModelTests.cpp | 14 | `BrainIdleAndPatrolSelectOnlyTheirOwnMovementIntent`, `BrainVisibleHostileChasesThenAttacksWithHysteresis`, `BrainLostSightInvestigatesAndNeverAttacksFromMemory`, `BrainMemoryExpiresIntoReturnAndDoesNotReacquireBeforeHome`, `BrainDestroyedTargetSkipsMemoryAndReturns`, `BrainHomeAndTargetLeashesForceReturn`, `BrainReturnArrivalResumesPatrolWithoutLosingCounters`, `BrainPatrolWaitThenPingPongDoesNotSkipEndpoints`, `BrainLoopAndSingleWaypointHaveBoundedArrivalEdges`, `BrainDeadStopsAndReviveReturnsWithoutOldMemory`, `BrainDisabledSuppressesAllIntent`, `BrainBlockedIsTerminalUntilExplicitReset`, `BrainInvalidSettingsAndInputsAreTransactional`, `BrainHitchExpiresMemoryWithoutUnboundedWaypointAdvances` |
| 22 | GameplayAITests.cpp | 24 | `AIGameplayPatrolUsesRealBox2DBodyAndWaypointWait`, `AIGameplayThreeDPatrolUsesBakedGridAndCharacter`, `AIGameplaySightChaseAttackAndLockedTargetDamage`, `AIGameplayOccludedTargetIsNotAcquiredInEitherDimension`, `AIGameplayLostTargetInvestigatesThenReturnsHome`, `AIGameplayTargetDestroySkipsMemoryAndUUIDReplacementIsFresh`, `AIGameplayLocalStallRetriesAreBoundedAndTerminal`, `AIGameplayDisableStopsImmediatelyAndRemoveRestoresNavSettings`, `AIGameplayDeathStopsNavigationAndReviveReturns`, `AIGameplayScenePauseFreezesMemoryAndPrunesDestroyedOwners`, `AIGameplayQueryBudgetDefersWithoutCrashingOrMoving`, `AIGameplayRehomeAndInvalidAbsoluteRouteAreTransactional`, `AIGameplayConfigurationRejectsWrongDimensionAndConflictingPatrol`, `AIGameplayHostDestructionReleasesAgentAndCrossSceneIsRejected`, `AIGameplayExplicitStrikeCannotHitACloserDifferentActor`, `AIGameplaySenseHonorsFOVNearRangeTagAndTeam`, `AIGameplayBlueprintStateAndEventsAreTyped`, `AIGameplayBrainCapacityAndEventTelemetryAreBounded`, `AIGameplayPlayRestartClearsContextAndRestoresAgentConfiguration`, `AIGameplayBlueprintOwnerPauseAndDisableSuspendTheController`, `AIGameplayWorkshopGraphsAndPrefabReferencesValidate`, `AIGameplayDynamicThreeDBodyStopsImmediatelyOnDisable`, `AIGameplayOptionalSelfPinsResolveToTheGraphHost`, `AIDelegatedAttackKeepsModesWithoutBuiltInStrike` |
| 23 | RulesModelTests.cpp | 20 | `RulesHealthShieldAbsorbsThenOverflowsIntoHealth`, `RulesHealthPiercingLeavesShieldIntact`, `RulesHealthGraceExpiresAtExactBoundary`, `RulesHealthZeroHitDoesNotStartInvulnerability`, `RulesHealthOverkillReportsOnlyActualLossAndDeathOnce`, `RulesHealthCannotHealDeadActor`, `RulesHealthHealingAndShieldRestorationAreCapped`, `RulesHealthInvalidInputsAreTransactional`, `RulesHealthInvalidConfigurationPreservesState`, `RulesHealthRespawnDelayAndGracePreserveDeathHistory`, `RulesHealthInvalidOrAliveReviveDoesNotMutate`, `RulesHealthExplicitGraceBypassStillRespectsShield`, `RulesBagRejectsUnsafeAndOversizedItemKeys`, `RulesBagAddsAndConsumesWithoutZeroSlots`, `RulesBagMissingKeyCannotAwardReward`, `RulesBagRewardOverflowDoesNotConsumeKey`, `RulesBagCanConsumeLastKeyToFreeRewardSlot`, `RulesBagNonConsumingRequirementAndSameItemExchange`, `RulesBagShrinkingLimitsRejectsWithoutLoss`, `RulesBagBoundaryCountsAndInvalidConfigurationAreTransactional` |
| 24 | GameplayRulesTests.cpp | 31 | `RulesWorldDifferentGraphHostsShareHealth`, `RulesWorldFriendlyFireAndEnvironmentDamage`, `RulesWorldPauseDoesNotExpireDamageGrace`, `RulesWorldResetOnStopAndPlayRestart`, `RulesWorldPrunesDestroyedAndRecreatedUUID`, `RulesWorldRejectsCrossSceneEntity`, `RulesWorldDeathEmitsOneOrderedEdgeAndBoundedEvents`, `RulesWorldDeadActorCannotMoveOrInteract`, `RulesWorldInteractionCollectsOnlyOnceAcrossHosts`, `RulesWorldFailedInteractionDoesNotConsumeOrExhaust`, `RulesWorldFullHealthDoesNotWasteMedkitOrKey`, `RulesWorldInteractionCooldownUsesSharedSceneClock`, `RulesWorldFindReportsLockedPromptWithDeterministicTie`, `RulesWorldInteractionRevalidatesDistanceAfterFind`, `RulesWorldFacingFilterAndInvalidQuery`, `RulesWorldTwoDLineOfSightAndBypass`, `RulesWorldThreeDLineOfSightUsesJolt`, `RulesWorldMeleeUsesFacingTeamAndCooldown`, `RulesWorldMissStillConsumesAttackCooldown`, `RulesWorldMeleeCannotHitThroughWall`, `RulesWorldQueriesAndActorRecordsAreBounded`, `RulesWorldRespawnRestoresCheckpointBeforeReviving`, `RulesWorldMissingCheckpointCannotPartiallyRevive`, `RulesWorldBlueprintInventoryAndEventPinsAreTyped`, `RulesWorkshopGraphsRoundTripAndValidate`, `RulesWorldInteractionInventoryEventsReflectCommittedExchange`, `RulesWorldInvalidBridgeNumbersCannotChangeInventory`, `RulesWorldRecreatedInteractionDoesNotInheritUses`, `RulesWorldPausedFrameRefreshesQueryBudgetWithoutAdvancingTimers`, `RulesWorldLethalDamageStopsIntentWithoutAnotherMotorInvocation`, `RulesWorldRespawnInvalidatesMotorStateWhenDeadFramesSkipTheMotor` |
| 25 | GameplayMathTests.cpp | 22 | `GameplayMotorAcceleratesAndDecelerates`, `GameplayMotorAnalogAndDiagonalSpeedsAreBounded`, `GameplayMotorAccelerationIsFramePartitionInvariant`, `GameplayMotorAirControlPreservesMomentum`, `GameplayMotorCoyoteJumpWorksWithinGrace`, `GameplayMotorExpiredCoyoteDoesNotJump`, `GameplayMotorBuffersJumpUntilLanding`, `GameplayMotorExpiredBufferDoesNotReplay`, `GameplayMotorJumpCutOnlyAppliesOnceToOwnedJump`, `GameplayMotorDoesNotDoubleJumpFromStaleGround`, `GameplayMotorDisableCancelsBufferedInput`, `GameplayMotorZeroDeltaAndInvalidInputAreTransactional`, `GameplayMotorHitchAccelerationIsBoundedAndTimersExpire`, `GameplayMotorHugeFiniteInputKeepsDirectionAndFiniteSpeed`, `GameplayCameraInitializesAndDampsWithoutFrameDependence`, `GameplayCameraSnapHandlesTeleport`, `GameplayCameraOrbitClampsPitchAndWrapsYaw`, `GameplayCameraZeroDeltaPreservesPoseAndInvalidIsTransactional`, `GameplayCheckpointRoundTripsFullActorPrecision`, `GameplayCheckpointRejectsMalformedWithoutReplacingLastGood`, `GameplayCheckpointRejectsWrongVectorAndActorTypes`, `GameplayCheckpointRejectsDeepPayloadAndInvalidIdentity` |
| 26 | GameplayRuntimeTests.cpp | 24 | `GameplayRuntimeMotor2DMovesAndStopsOnDisable`, `GameplayRuntimeTopDownStopCancelsBothAxes`, `GameplayRuntimeMotor2DIgnoresVerticalInputForWalkingSpeed`, `GameplayRuntimeMotorRejectsDuplicateAdvance`, `GameplayRuntimeMotor3DUsesRealCharacterAndJump`, `GameplayRuntimeAnimatorReadsCompletedPhysicsNotNew2DCommand`, `GameplayRuntimeAnimatorBindingIsAtomicForWrongTypes`, `GameplayRuntimeAnimatorBindingRejectsExplicitMissingSource`, `GameplayRuntimeCameraFollowsAndClamps2DBounds`, `GameplayRuntimeCameraObstructionIgnoresPlayerButStopsAtWall`, `GameplayRuntimeCheckpointRestores2DPhysicsAndClearsVelocity`, `GameplayRuntimeCheckpointRestoresCharacterAndResetsIntent`, `GameplayRuntimePersistentCheckpointLoadsIntoFreshRuntime`, `GameplayRuntimeCheckpointIdentityMismatchAndCorruptionDoNotMoveActor`, `GameplayRuntimeFailedPersistentCaptureKeepsMemoryCheckpoint`, `GameplayRuntimeCheckpointClearAndSlotValidation`, `GameplayRuntimeCheckpointIOBudgetAndCacheAreBounded`, `GameplayRuntimeRejectsCrossSceneTargets`, `GameplayRuntimeCheckpointRejectsObstructedRestore`, `GameplayRuntimeTypingBlocksCompositeInput`, `GameplayRuntimeWallCollisionDoesNotAnimateCommandedSpeed`, `GameplayRuntimeDestroyedMotorDoesNotStopReplacementWithSameUUID`, `GameplayWorkshopGraphsValidateAndTextEqualityIsExact`, `GameplayRuntimeNativeHostDisableAndDebuggerPauseStopMotor` |
| 27 | AnimatorControllerTests.cpp | 21 | `Animator2DWeightsResolveExactSamples`, `Animator2DWeightsInterpolateAxisAndClampOutside`, `Animator2DWeightsHaveSymmetryAndPartitionUnity`, `Animator2DWeightsIgnoreDuplicateAndInvalidSamples`, `Animator2DWeightsHandleEmptySingletonAndBudget`, `Animator2DWeightsAreStableUnderTranslationAndScale`, `AnimatorParametersEnforceTypesAndFiniteValues`, `AnimatorDampingIsFramePartitionInvariant`, `AnimatorDampingRejectsInvalidInputAndPreservesValue`, `AnimatorPriorityWinsAndTiesKeepAuthorOrder`, `AnimatorOnlyWinningTransitionConsumesItsTriggers`, `AnimatorFadeInterruptionPolicyKeepsTriggerPending`, `AnimatorMissingOrMistypedConditionsNeverPass`, `AnimatorExitTimeAndInvalidTransitionDoNotConsumeTriggers`, `AnimatorStateEntryRestartsSameClipAndSelf`, `AnimatorModelFreeClockAndDirectModeResume`, `Animator2DStateWeightsUseBothFloatAxes`, `AnimatorControllerValidationReportsNamesTypesAndReferences`, `AnimatorControllerValidationReportsInvalidBlendSamples`, `AnimatorControllerValidationAndBlendBudgetsAreBounded`, `AnimatorBlendEntryPreservesLegacyClipField` |
| 28 | AnimatorRuntimeTests.cpp | 15 | `Animator2DRuntimeBlendsSkeletalPosesAndKeepsPhase`, `Animator2DRuntimeRootMotionUsesWeightedClipDelta`, `AnimatorModelFreeStatesRespectDurationAndResetSharedClipClock`, `AnimatorModelFreeBlendTreeHasARealNormalizedClock`, `AnimatorControllerSerializationKeeps2DAndTransitionPolicy`, `AnimatorControllerLegacySceneStillUses1DAndOrderedTransitions`, `AnimatorBlueprintParametersAndSuccessOutputsDriveRealComponent`, `AnimatorBlueprintRejectsWrongSetterTypeWithoutChangingParameter`, `AnimatorBlueprintDirectClipPausesControllerAndStateResumesIt`, `AnimatorModelFreeClockEmitsBlueprintAnimationEvents`, `AnimatorSpriteClipsFollowTheStateClockDominantMotionAndFade`, `AnimatorSpriteClipsFireFrameEventsOncePerCrossingAtAnyRate`, `AnimatorSpriteClipsDirectionSwitchesFireEachStepOnce`, `AnimatorSpriteClipsSerializeAndResetRuntime`, `AnimatorLayerRestartReplaysTheClipAndItsEvents` |
| 29 | BlueprintTests.cpp | 30 | `BlueprintTypedFunctionReturnsAndLocals`, `BlueprintTypedFunctionDelayPreservesArguments`, `BlueprintSignaturesRoundTripAndRename`, `BlueprintFunctionParameterCannotEscape`, `BlueprintMissingReturnFailsClearly`, `BlueprintArrayForEachUsesFrozenValues`, `BlueprintArrayAppendAndCheckedIndex`, `BlueprintArrayTypesAndBudgets`, `BlueprintCustomEventsAreDeferredAndNamed`, `BlueprintClipboardIsAtomicAndRemapsIDs`, `BlueprintClipboardClonesWholeTypedFunction`, `BlueprintFunctionPinChangePrunesInvalidLinks`, `BlueprintCollectionOperationsAcrossAllElementTypes`, `BlueprintCollectionBoundsRejectWithoutMutation`, `BlueprintConcurrentDelayedCallsHaveIsolatedLocals`, `BlueprintNestedFunctionRestoresCallerArgumentsAndLocals`, `BlueprintRecursiveCallsHaveBoundedDepthAndStackDiagnostic`, `BlueprintVoidFunctionMayFallThrough`, `BlueprintBreakCannotEscapeFunctionIntoCallerLoop`, `BlueprintInvalidSignaturesAndScopesAreRejected`, `BlueprintClipboardConflictNamesDoNotCascade`, `BlueprintClipboardRejectsIncompatibleExistingFunctionAtomically`, `BlueprintCustomEventPayloadAndPendingLimits`, `BlueprintFiniteRecursionRestoresEachInvocationResults`, `BlueprintForEachBreakPublishesCompletedExactlyOnce`, `BlueprintRetainedPayloadIsBoundedAcrossPendingEvents`, `BlueprintArrayArgumentsAndMultipleReturnsRemainTyped`, `BlueprintFunctionCannotCaptureCallerExecutionResults`, `BlueprintEmptyHostPayloadKeepsLegacyEventCompatibility`, `BlueprintUserFunctionNamesCannotAliasEventScopes` |
| 30 | NavRecoveryTests.cpp | 10 | `NavRecoveryBoundsRetriesAndBecomesTerminal`, `NavRecoveryProgressAccumulatesAndClearsAttempts`, `NavRecoverySmallJitterCannotResetWatchdog`, `NavRecoveryPendingQueriesDoNotConsumeRetryBudget`, `NavRecoveryPortalAndZeroSpeedArePaused`, `NavRecoveryCooldownDoesNotCreatePerFrameRetryLoop`, `NavRecoveryResetAndArrivalKeepExplicitState`, `NavRecoveryInvalidInputStopsAndDisabledIsHarmless`, `NavRecoveryDeltaIsClampedAndZeroDoesNotAdvanceTimers`, `NavRecoveryWaitingDoesNotResetExistingAttemptCount` |
| 31 | NavCrowdTests.cpp | 17 | `NavCrowdIsolatedAgentKeepsPreferredAndSpeedCap`, `NavCrowdHeadOnPairPassesWithoutOverlap`, `NavCrowdFourWayCrossingReachesGoals`, `NavCrowdPermutationDoesNotChangeVelocities`, `NavCrowdStationaryAndDifferentFloors`, `NavCrowdOverlappingPairSeparatesDeterministically`, `NavCrowdNeighborAndCandidateOverflowStopVisibly`, `NavCrowdInvalidInputAndCapacityFailClosed`, `NavCrowdSparseBroadphaseSkipsDistantActors`, `NavAtlasSegmentRejectsHolesWallsCornersAndBudgets`, `NavAtlasSegmentClearanceAndWeightedCostAreConservative`, `NavAtlasSmoothingReducesCornersAndPreservesPortalHandshake`, `NavAtlasWorkerSmoothingOptionAndCancellation`, `NavAtlasSegmentChecksFloorClimbAndStartHeight`, `NavAtlasSmoothingKeepsWeightedDetourAndStaleRoutes`, `NavCrowdSettingsClampAndNonfiniteReject`, `NavCrowdMovingUnresponsiveObstacleIsPredicted` |
| 32 | NavQueryQueueTests.cpp | 10 | `NavQueryQueueStartAndPublicationBudgetsAreSeparate`, `NavQueryQueueLatestRequestReplacesQueuedWorkWithinCapacity`, `NavQueryQueueSharesSnapshotAccountingAndReleasesCancelledPayloads`, `NavQueryQueueSnapshotBudgetRejectsWithoutDestroyingPreviousRequest`, `NavQueryQueueResultByteBudgetFailsExplicitlyAndDoesNotLeak`, `NavQueryQueuePointAndExpansionLimitsAreEnforced`, `NavQueryQueueCancelsRunningSearchAndRetainsCapacityUntilAcknowledged`, `NavQueryQueueShutdownJoinsAnActiveCancelledSearch`, `NavQueryQueueRejectsInvalidInputAndPrecancelledAtlasSearch`, `NavQueryQueueMixedReplacementCyclesStayBounded` |
| 33 | NavAtlasTests.cpp | 16 | `NavAtlasSparseGapsUseOnlyLoadedCells`, `NavAtlasAdjacentTilesMatchDensePathCost`, `NavAtlasOneWayLinksAndInactiveEndpoints`, `NavAtlasCheapPortalAwayFromGoalRemainsOptimal`, `NavAtlasBuildIsTransactionalAndCopiesSources`, `NavAtlasRejectsInvalidTilesAndLinks`, `NavAtlasClimbCostsAndCornerRulesCrossTiles`, `NavAtlasSnapHeightBoundsAndExpansionLimit`, `NavAtlasCarvesAndRetainedSnapshots`, `NavAtlasDeterministicParallelQueries`, `NavRouteFollowerRequiresExplicitTraversalAndRejectsStale`, `NavRouteFollowerPartialDistanceAndInvalidInput`, `NavAtlasRandomizedDenseCostOracle`, `NavAtlasSourceOrderingAndBudgetedLargeWorld`, `NavAtlasRejectsCollapsedFloatCellCentres`, `NavAtlasAndFollowerLargeFiniteHeightDoesNotOverflow` |
| 34 | the framework itself (TestFramework.h) | 3 | `TestFrameworkPatterns`, `TestFrameworkOptions`, `TestFrameworkReports` |
| 35 | Main (scene, scripts, physics, prefabs, particles) | 25 | `UUIDsAreUnique`, `UUIDTextAndHashing`, `EntityLifecycle`, `SerializerRoundTrip`, `SceneCopyIsIndependent`, `ScriptLifecycleAndFields`, `PhysicsGravityAndCollision`, `InstantiateAndDeferredDestroy`, `DecomposeTransform`, `ProjectTemplate`, `HierarchyWorldTransforms`, `PrefabsAndClipboard`, `SpriteAnimationFrames`, `Physics3D`, `Raycast2DAndSceneRequests`, `Kinematic3DFollowsTransformAtLowFPS`, `TilemapCollisionAndResize`, `ParticlesSimulate`, `ParticleSimulationUnchanged`, `ParticleGradientStops`, `ParticleCurvesAndSize`, `ParticleVelocityOverLifetime`, `ParticleTextureSheetFrames`, `ParticleConeAndEdgeShapes`, `ParticleModulesSerialization` |
| 36 | M14 Particle II: mesh shape, sub-emitters, collisions, noise, trails | 29 | `ParticleMeshEmitterSampling`, `ParticleSubEmitters`, `ParticleCollisionPlane`, `ParticleCollisionWorld`, `ParticleNoise`, `ParticleTrails`, `ParticleModulesSerializationII`, `ParticleBurstsList`, `ParticleDurationDelayAndSpeed`, `ParticlePrewarm`, `ParticleSortOrder`, `ParticleSimulationSpaces`, `ParticleStopActions`, `ParticleStopActionWaitsForSubEmitters`, `ParticleStartBurstWaitsForDelay`, `ParticlePlayOnStart`, `ParticleMainSerialization`, `ParticleModuleSwitchesAndPlayback`, `ParticleNearFadeAndStretchedBounds`, `ParticleFollowCameraWeather`, `ParticleBuiltinPresets`, `ParticlePresetFiles`, `ParticleSerializationEverything`, `ParticleSampleScenesRoundTrip`, `ParticlePerformance`, `SkeletalPoseEvaluation`, `CharacterController3D`, `AnimatorStateMachine`, `IrradianceSH` |
| 37 | EntityRef fields | 2 | `EntityRefSerialization`, `EntityRefRemapOnCopy` |
| 38 | timers (Invoke) | 1 | `InvokeTimers` |
| 39 | tweens / easing | 2 | `EasingFunctions`, `TweensRunAndChain` |
| 40 | input action map | 13 | `InputActionMapDefaults`, `InputBindingNames`, `InputActionMapSerialization`, `ProjectInputMapRoundTrip`, `InputKeyAndMouseFrames`, `InputBindingHints`, `InputActiveDeviceAndTextHints`, `InputSimulatedKeys`, `InputActionQueries`, `InputAxes`, `SaveDataValuesAndFile`, `SaveDataSubmitRecord`, `SaveDataProjectLocation` |
| 41 | M01: Random | 3 | `RandomDeterminism`, `RandomRanges`, `RandomGeometry` |
| 42 | M01: tags | 2 | `TagsQueries`, `ProjectTagList` |
| 43 | M01: messages / events | 3 | `ScriptMessages`, `EventBusBasics`, `EventBusInScene` |
| 44 | M01: hierarchy queries | 1 | `HierarchyQueries` |
| 45 | M01: name cache | 1 | `FindEntityNameCache` |
| 46 | script lifecycle (OnEnable / OnStart / OnDisable) | 3 | `ScriptLifecycleOrder`, `ScriptLifecycleEdgeCases`, `DestroyKeepsOtherScriptsAlive` |
| 47 | triggers (OnTriggerBegin / OnTriggerEnd) | 3 | `TriggerCallbacks2D`, `TriggerEndWhenDestroyed`, `TriggerCallbacks3D` |
| 48 | script field attributes (enum, color, range, tooltip) | 1 | `ScriptFieldEnumColorRange` |
| 49 | new-script templates | 1 | `ScriptTemplates` |
| 50 | script component removed / replaced at runtime | 2 | `ScriptComponentRemovedAtRuntime`, `DestroyEntityReentrant` |
| 51 | M02: editor UX (hierarchy order, multi-selection) | 7 | `HierarchySiblingOrder`, `SceneOrderStableOnDestroy`, `HierarchyOrderSerialization`, `MultiEntityDuplicateDestroy`, `EntitySelectionModel`, `GroupTransformEditing`, `TransformClipboardText` |
| 52 | M02: viewport (snap, grid, camera) | 8 | `EditorSnapSettings`, `EditorViewSettingsJson`, `EditorCameraBookmarkTransition`, `EditorCameraFrameBounds`, `NameLookupFollowsSceneOrder`, `DestroyKeepsSceneOrder`, `SceneBoundsForFraming`, `GizmoDuplicateWhileDragging` |
| 53 | M02: inspector / hierarchy (31-35) | 5 | `ComponentKeysMatchSceneFiles`, `InspectorComponentOrder`, `ComponentClipboardCopyPaste`, `EditorHiddenAndLockedFlags`, `InspectorFoldStateJson` |
| 54 | content browser / assets | 7 | `AssetTypesAndFilter`, `AssetListingAndSorting`, `AssetJsonReferenceRewrite`, `AssetMoveUpdatesReferences`, `AssetMoveKeepsNamesAndTags`, `AssetMoveUpdatesParticlePresets`, `AssetImportFromOutside` |
| 55 | asset registry, .zmeta, file watcher | 20 | `AssetTypeDetection`, `AssetHandleText`, `AssetMetaFileRoundTrip`, `AssetRegistryScanAndLookup`, `AssetRegistryFollowsFileChanges`, `AssetHotReloadAppliesChanges`, `ProjectAssetRegistry`, `AssetRegistrySnapshots`, `ProjectAssetRegistryCache`, `FileChangeQueueDebounce`, `FileWatcherSeesChanges`, `TextureImportSettingsInMeta`, `ModelImportSettingsInMeta`, `ModelImportTransformSkinning`, `ImportSettingsOfProjectAssets`, `AssetReferencesFollowUUIDs`, `AssetMovesOutsideEditorAreDetected`, `AssetSearchByPath`, `ContentBrowserSettingsJson`, `SelectionSummaryForStatusBar` |
| 56 | active / inactive entities (M03) | 11 | `ActiveFlagAndHierarchy`, `InactiveEntitiesAreNotDrawn`, `InactiveEntitiesLeavePhysics2D`, `InactiveEntitiesEndContacts`, `InactiveEntitiesLeavePhysics3D`, `InactiveEntityScripts`, `ScriptsSwitchingEntitiesOff`, `ScriptActiveApi`, `ActiveOnEnableOnDisable`, `ActiveStateSerialization`, `SelectionSummaryInactive` |
| 57 | M03: what else an inactive entity stops | 37 | `InactiveParticlesStop`, `InactiveAudioSourcesStop`, `AudioMixerGroupTeardown`, `AudioMixerGroupMath`, `AudioMixerGroupsDeviceless`, `AudioMusicCrossfade`, `AudioListenerSelection`, `ProjectAudioSettings`, `AudioRandomVariation`, `AudioVoiceLimit`, `AudioSourceScriptControl`, `AudioDoppler`, `AudioFilterMath`, `AudioFiltersDeviceless`, `AudioFades`, `AudioClipImportSettings`, `AudioPlayerSettingsSaveLoad`, `AudioProfiler`, `AudioWaveformViewAndLoopRegion`, `AudioDirectionalCone`, `AudioAmbientZones`, `AudioOnSoundFinished`, `InactiveAnimatorsPause`, `InactiveUIButtons`, `InactiveCamerasDoNotCount`, `InactiveLightsDoNotCount`, `SpotLightComponentRoundTrip`, `SpotLightCone`, `FxaaSceneSetting`, `LocalLightSelection`, `InactiveTilemapCollision`, `InactiveCharacterControllers`, `CharacterContactsSwitchedOffAndOnInOneFrame`, `ContactsOfBodiesReplacedDuringDispatch`, `PrefabInstancesKeepActiveState`, `InstantiateFromTransformedInactiveGroup`, `ActivityHierarchyPhysicsScripts` |
| 58 | asset inspector / references | 7 | `AssetInfoImages`, `AssetInfoAudio`, `AssetInfoModels`, `AssetReferenceIndexScans`, `AssetImportLogReportsOnce`, `AssetDeletionWarnings`, `AssetThumbnailsAndDiskCache` |
| 59 | materials (.zmat) | 10 | `MaterialFormatRoundtrip`, `MaterialAssetLibraryAndReload`, `MeshRendererMaterialAssets`, `TransparentSortBackToFront`, `MaterialSurfaceStates`, `PrimitiveWindingFacesOutward`, `GLTFMaterialModes`, `InlineMaterialModesRoundtrip`, `MaterialInstancesCopyOnWrite`, `GLTFMaterialExport` |
| 60 | M05: tasks 096-100 | 22 | `TransparentSortPriority`, `MaterialEmission`, `MaterialSerializationFields`, `MaterialSerializationRobust`, `MaterialSceneSerialization`, `MaterialPresetLibrary`, `MaterialThumbnailStamp`, `RenderScaleResolution`, `RenderScaleSceneSetting`, `AutoExposureMath`, `AutoExposureSceneSetting`, `FrustumCullingVolume`, `CullingBounds`, `EditorDebugViews`, `Culling2DBounds`, `MeshInstancingBatches`, `ShadowQualitySettings`, `ShadowSettingsSceneSetting`, `RenderStatsSummary`, `MultipleDirectionalLights`, `SkySettingsSceneSetting`, `LensEffectsSceneSetting` |
| 61 | debug drawing (M07) | 19 | `DebugDrawShapes`, `DebugDrawDurations`, `DebugDrawFollowsGameTime`, `DebugDrawRaycasts`, `DebugDrawContacts`, `DebugDrawShippingIsEmpty`, `DebugDrawSettingsSaved`, `DebugDrawVelocities`, `DebugDrawNavigationPath`, `FrustumCornersCamera`, `EditorGizmoSettingsSaved`, `TransformGizmoSettingsSaved`, `SelectionOutlineSettingsSaved`, `EditorBoundsAndSkeletonGizmosSaved`, `SelectionOutlineLookup`, `SelectionOutlineCoverage`, `SkeletonJointTransforms`, `EntityLocalBounds`, `ImageCompare` |
| 62 | M07: profiler tree, overlay, normals, queue | 7 | `ProfilerHierarchy`, `ProfilerScopedTimersAndAverages`, `FrameTimeHistoryStats`, `StatsOverlaySettingsSaved`, `VertexNormalLines`, `DebugDrawPausedFramesDoNotPileUp`, `DebugDrawQueueLimitsAndShortDurations` |
| 63 | M08: 2D (sprite sheets, 9-slice, sorting layers, parallax) | 13 | `SpriteSheetSliceGrid`, `SpriteFrameUVAndRegions`, `SpriteSheetStorageAndSequences`, `SpriteSheetFromAssetRegistry`, `SpriteGeometryNineSlice`, `SpriteGeometryTiled`, `SpriteRendererRectAndBounds`, `SortingLayerListEdits`, `SortKey2DOrder`, `SortingLayersInProjectFile`, `Scene2DFieldsSerialized`, `ParallaxOffsetAndRepeat`, `ParallaxCopiedWithScene` |
| 64 | M08: 2D camera follow and shake | 6 | `Camera2DFollowMath`, `Camera2DFollowStep`, `Camera2DFollowInScene`, `CameraShakeTrauma`, `CameraShakeInScene`, `EnglishContentDefaults` |
| 65 | M08: tilemap layers, auto-tiles, tools | 16 | `TilemapLayersEdit`, `TilemapLayersSerializedAndSolid`, `AutoTileMasks`, `AutoTilePaintEdges4`, `AutoTilePaintBlob8AndFallback`, `AutoTileSetStorage`, `TilemapFloodFillRectAndLine`, `TilemapSampleAutoTilesConsistent`, `TilesetPropertiesStorage`, `TilemapCollisionBoxes`, `OneWayPlatformDecision`, `OneWayBoxColliderInScene`, `TilemapTilePropertiesInScene`, `TileBrushStamp`, `SpriteFlashAndOutline`, `SpriteOutlineDrawModes` |
| 66 | M08: tint animation, 2D lights, pixel perfect, Y-sort | 8 | `TintGradientAndPhases`, `TintAnimationInScene`, `TintAnimationOnSpawnedEntities`, `Light2DMathAndGathering`, `PixelPerfectCamera`, `PixelPerfectCameraOddSizes`, `YSortedSortingLayers`, `TopDown2DSampleScene` |
| 67 | 2D joints (M09) | 9 | `DistanceJoint2DRodRopeAndWorldAnchor`, `DistanceJoint2DSpring`, `RevoluteJoint2DLimitAndMotor`, `PrismaticJoint2DAxisLimitAndMotor`, `WeldJoint2DRigidAndSoft`, `WheelJoint2DCarDrives`, `Joint2DBreakForceAndScriptCallback`, `Joint2DFollowsBodiesInAndOut`, `Joint2DSerializeAndRemap` |
| 68 | 2D colliders, materials, layers, platforms (M09) | 8 | `Polygon2DDecomposition`, `Polygon2DTraceAlphaOutline`, `PolygonCollider2DInScene`, `EdgeCollider2DInScene`, `PhysicsMaterialCombineAndJson`, `PhysicsMaterialAssetOnColliders`, `CollisionLayers2DMatrixAndFiltering`, `PlatformEffector2DInScene` |
| 69 | M09: effectors, queries, filtering (171-175) | 6 | `Polygon2DClipping`, `AreaEffector2DPushesBodies`, `BuoyancyEffector2DFloats`, `Overlap2DQueries`, `Raycast2DAllOrdering`, `CollisionFiltering2D` |
| 70 | 2D physics settings, sleeping, joint prefabs, ragdoll (M09) | 6 | `Physics2DStepSettingsAndSubSteps`, `Physics2DSleeping`, `Physics2DScriptForcesAcrossSteps`, `Physics2DOwnSleepTimingWakeAndSwitch`, `Joint2DPrefabFileAndSpawn`, `Ragdoll2DSampleScene` |
| 71 | 3D joints and convex hulls (M10) | 14 | `FixedJoint3DHoldsAndWelds`, `HingeJoint3DPendulumLimitsMotorSpring`, `HingeJoint3DSlowSpringDoesNotSleepShort`, `Joint3DStartAsleepBodiesStayAsleep`, `DistanceJoint3DRodRopeAndSpring`, `SliderJoint3DAxisLimitsAndMotor`, `Joint3DBreakForceAndScriptCallback`, `Joint3DFollowsBodiesInAndOut`, `Joint3DCollideConnected`, `ConeJoint3DSwingTwistAndFriction`, `Joint3DSerializeAndRemap`, `ConvexHullBuildAndCache`, `Joints3DSampleScene`, `Ragdoll3DSampleScene` |
| 72 | M10: mesh colliders, layers, 3D queries | 6 | `MeshCollider3DStaticAndCached`, `CollisionLayers3DMatrixAndFiltering`, `Overlap3DQueries`, `ShapeCast3DQueries`, `Raycast3DAllOrdering`, `Queries3DSampleScene` |
| 73 | 3D physics: CCD, sleeping, materials, triggers, pushing (M10) | 7 | `ContinuousCollision3DStopsFastBodies`, `Physics3DSleepingAndWakeUp`, `PhysicsMaterial3DOnColliders`, `Triggers3DMixedCollidersAndCharacters`, `CharacterController3DPushesBodies`, `Triggers3DContactsOfSleepingBodies`, `Physics3DSampleScene` |
| 74 | 3D physics II: debug contacts, joint debug, joint prefabs, script queries (M10) | 6 | `Physics3DDebugContactData`, `DebugDrawJoints`, `Joint3DPrefabFileAndSpawn`, `ScriptQueries3D`, `PhysicsDebug3DSampleScene`, `AudioSampleScene` |
| 75 | M12: UI widgets (image, slider, toggle, progress bar, input field) | 11 | `UIImageFillGeometry`, `UISliderValueMapping`, `UISliderDragging`, `UIToggleClicksAndGroups`, `UIImageBlocksTheMouse`, `UIProgressBarFollowsValue`, `TextEditUTF8`, `UIInputFieldEditing`, `UIWidgetSerialization`, `TextInputEventStream`, `UIWidgetsSampleScene` |
| 76 | M12: UI layout, canvas, layout groups, events, navigation (226-230, 236) | 11 | `UIRectLayoutMath`, `UICanvasScaleMath`, `UIHierarchyLayout`, `UICanvasScalesTheUI`, `UILayoutGroupMath`, `UILayoutGroupsInTheScene`, `UIPointerEventsReachScripts`, `UIKeyboardAndGamepadNavigation`, `UINavigationIntoInputFields`, `UILayoutSerialization`, `UILayoutSampleScene` |
| 77 | M12: text layout, font atlases | 5 | `TextLayoutAlignment`, `TextLayoutWordWrap`, `TextLayoutOverflowModes`, `TextComponentAlignmentSerialization`, `FontAtlasSizeLadder` |
| 78 | M12: UI animations | 5 | `UIAnimationMath`, `UILookScalesAndFadesWithChildren`, `UITweensRunOnUITime`, `UIAnimationShowHideAndFeedback`, `UIAnimationSerialization` |
| 79 | M12: editor UI picking (rect) and its coordinates | 1 | `UIRectPickingForTheEditor` |
| 80 | M12: masks and scroll views | 7 | `UIScrollMath`, `UIMaskClipsDrawingAndPicking`, `UIScrollViewLayoutAndWheel`, `UIScrollViewDragging`, `UIScrollViewScrollbarAndNavigation`, `UIScrollViewWithAViewport`, `UIScrollViewCopiesAndClones` |
| 81 | M12: game time (pause menus), display options | 6 | `GameTimeScaleAndPause`, `DisplaySettingsAreSaved`, `PauseMenuSampleScene`, `UIEventOrderInScrollViews`, `UIScrollViewDragOfItsBackgroundIsNoClick`, `UIScrollViewHorizontalInACanvas` |
| 82 | property animation (M13) | 9 | `PropertyClipInterpolation`, `PropertyClipFileRoundtrip`, `AnimationEventsFireOnce`, `AnimationPlayerPlaysClips`, `RootMotionExtraction`, `BlendTree1DWeights`, `AnimatorBlendTreeSyncsClips`, `AnimatorLayersMasks`, `SpriteClipAssetsPlayWithEvents` |
| 83 | M13: clip trimming / sub-clips (254) | 2 | `AnimationClipTrimming`, `ModelClipSettingsSplitClips` |
| 84 | M13: glTF morph targets (255) | 2 | `MorphTargetsApplyWeights`, `MorphWeightsFromClipsAndScripts` |
| 85 | M13: skeletal scrub preview (251) | 1 | `AnimatorEditorPreviewScrub` |
| 86 | M13: bone attachments (252) | 1 | `BoneAttachmentFollowsJoint` |
| 87 | M13: footsteps from animation events (253) | 1 | `FootstepsFromAnimationEvents` |
| 88 | M13: multiple skins, pose cache, culling | 4 | `MultipleSkinsInOneGLTF`, `AnimationPoseCache`, `SkinJointInfluencesFitPalette`, `AnimatorSkipsUnchangedAndCulledPoses` |
| 89 | M13: animation tests (258) - every property a clip animates, events through the scene | 3 | `PropertyClipAnimatesEveryProperty`, `AnimationEventsExactlyOnceInScene`, `AnimationPlayerPlayFromTurnsRound` |
| 90 | M15: scene management | 6 | `SceneManagerAdditiveScenes`, `SceneManagerDontDestroyOnLoad`, `SceneManagerAsyncLoad`, `SceneManagerStartFromBackgroundLoad`, `SceneManagerFadeTransition`, `BuildSceneList` |
| 91 | M15: scene events, build index, queries, load errors, stable files | 15 | `SceneEventsOrder`, `LoadSceneByBuildIndex`, `SceneQueryIndices`, `SceneLoadErrors`, `SceneSaveDeterministic`, `SceneVersionMigration`, `SceneMetadataAndThumbnail`, `ServicesRegistry`, `GlobalGameState`, `SceneLoadTimingBreakdown`, `SceneBinaryCache`, `LargeSceneSerialization`, `SubScenes`, `SubSceneFileMoveUpdatesReference`, `ReadTextFileFolder` |
| 92 | M15: scene management end to end | 1 | `SceneManagementFlow` |
| 93 | M17: project settings (quality, input, layers) | 21 | `QualityLevelLimits`, `QualitySettingsJson`, `QualityRuntimeLevel`, `ProjectQualityAndSections`, `InputVectorsComposite`, `InputBindingConflicts`, `CollisionLayerPresets`, `SceneFileTagReferences`, `ProjectSettingsIndexSearch`, `SemanticVersionParseCompare`, `PlayerSettingsSerialization`, `PlayerCommandLineParser`, `PlayerLaunchSettingsPrecedence`, `PlayerSettingsFullscreenToggleAndSplash`, `FramePacingPlanAndPacer`, `SplashTimelineAndLogo`, `InputAnyKeyAndMonitorPlacement`, `PlayerCompanySaveDataAndVersion`, `GameIconImagesAndDesktopEntry`, `ProjectFileMigrationAndUnknownKeys`, `BuildSceneChecksAndStartScene` |
| 94 | M17: graphics context, time, default gravity | 7 | `GraphicsContextSettings`, `TimeSettingsAndClock`, `TimeCaptureFrameRate`, `TimeApiTimeScaleAndUnscaledTimers`, `ProjectDescriptionThumbnailAndSummary`, `ProjectDefaultGravityAndStep`, `ProjectSettingsFullRoundTrip` |
| 95 | M17: audio defaults, localization, editor language | 7 | `AudioSpatialDefaults`, `LocalizationStringFiles`, `LocalizationLookupFormatPlural`, `LocalizationTableEditingAndFiles`, `LocalizedTextComponentAndScripts`, `ProjectLocalizationSettings`, `EditorLanguageStrings` |
| 96 | M18: the player's log file and crash reports | 2 | `PlayerLogFileRotation`, `CrashHandlerReports` |
| 97 | M18: the cursor API for scripts | 1 | `CursorApiForScripts` |
| 98 | M18: window size changes, a fixed aspect ratio (letterbox) | 1 | `PlayerViewportFixedAspect` |
| 99 | M18: the export leaves out unused assets | 11 | `ExportExcludesUnusedAssets`, `ExportReleaseModuleAndStrip`, `ExportDesktopEntryRelocatableAndIcons`, `ExportReportSizesAndFiles`, `PauseInBackgroundAudioOutput`, `DisplayGammaAndBrightness`, `PlayerBenchmarkStatsAndLimits`, `BuildInfoAndVersionText`, `ExportFolderSelfCheck`, `DisplayResolutionSettings`, `GameMenuSampleScene` |
| 100 | M19: noise | 2 | `NoiseRangeContinuityDeterminism`, `NoiseFractalsAndTiling` |
| 101 | M19: AnimationCurve | 2 | `AnimationCurveEvaluateAndWrap`, `AnimationCurveScriptField` |
| 102 | M19: splines | 5 | `SplineEvaluation`, `SplineArcLengthAndClosest`, `SplineComponentsSerializeAndFollow`, `SplineFollowerRuntimeStateClearedOnCopy`, `SplineFollowerMovesKinematicBodies` |
| 103 | M19: colours | 2 | `ColorConversions`, `SplineSampleScenes` |
| 104 | M19: math utilities (366) | 4 | `MathUtilsRanges`, `MathUtilsWrapping`, `MathUtilsMoveTowards`, `MathUtilsSmoothDamp` |
| 105 | M19: quaternion helpers (367) | 3 | `QuaternionEulerConvention`, `QuaternionLookAndFromTo`, `QuaternionSlerpRotateTowards` |
| 106 | M19: job system (368) | 4 | `JobSystemInlineWithoutWorkers`, `JobSystemParallelForCorrectness`, `JobSystemDependencies`, `JobSystemSpeedupAndStats` |
| 107 | M19: parallel particles and animators (369) | 2 | `JobSystemParticlesMatchSerial`, `JobSystemAnimatorsMatchSerial` |
| 108 | M19: memory statistics (370) | 3 | `MemoryStatsCategories`, `MemoryStatsProcess`, `MemoryStatsScenesAndPoseCaches` |
| 109 | M19: StringUtils | 2 | `StringUtilsBasics`, `StringUtilsUtf8` |
| 110 | M19: Stopwatch, ScopedLogTimer, Timer | 1 | `StopwatchAndTimers` |
| 111 | M19: log levels per logger, the log file's level, --log-level | 1 | `LogLevelsPerLogger` |
| 112 | M19: asserts with messages | 9 | `AssertsWithMessages`, `FileSystemLexicalPaths`, `FileSystemDiskPathsAndFolders`, `GeometryBoundingVolumes`, `GeometryRayAABBAndSphere`, `GeometryRayPlaneAndTriangle`, `GeometryOverlapsAndClosestPoints`, `MathPropertiesRandomized`, `GeometrySampleScene` |
| 113 | navigation (M20) | 16 | `NavGridBasics`, `NavAStarOptimalAgainstDijkstra`, `NavAStarCornersAndNeighbours`, `NavPartialPaths`, `NavLineOfSightAndErosion`, `NavStringPullingAndSpline`, `NavGridBuilderShapes`, `NavGridFromTilemap`, `NavGrid2DBakeInScene`, `NavGrid3DBakeOnColliders`, `NavAgent2DWalksToDestination`, `NavAgentFollowsTargetAndRepaths`, `NavAgent3DWithCharacterController`, `NavComponentsSerializationAndCopies`, `NavScriptApi`, `NavSampleScene3D` |
| 114 | M20: dynamic obstacles (386) | 3 | `NavGridCarving`, `NavObstacleCarvesAndAgentsRepath`, `NavObstacle3DOnFloor` |
| 115 | M20: steering (387) | 1 | `SteeringBehaviours` |
| 116 | M20: obstacle avoidance (388) | 2 | `SteeringFeelers`, `NavAgentAvoidsMovingBodies` |
| 117 | M20: AI state machine (389) | 1 | `AIStateMachineTransitions` |
| 118 | M20: sight (390) | 4 | `SightConeMath`, `SightSensor2DWithWalls`, `SightSensor3DWithWalls`, `SightScriptCallbacksAndAIComponentsSerialization` |
| 119 | M20: patrol routes, navigation debug drawing | 21 | `NavRandomGridsSmoothingAndCornerRules`, `PatrolPathModesAndWaits`, `PatrolPathWithNavAgent`, `PatrolPathChildrenAndSplineSources`, `PatrolScriptApiEventsAndSerialization`, `NavigationDebugDrawing`, `PatrolSampleScenes`, `BehaviorTreeComposites`, `BehaviorTreeDecoratorsParallelAndBlackboard`, `FormationSlotsAndAssignment`, `FormationMembersFollowTheLeader`, `FormationWithNavAgentsOnGrid`, `FormationSerializationAndCopies`, `FormationSampleScene`, `NavBakeFileFormat`, `NavBakeSavedWithScene`, `NavigationComponentsRoundTrip`, `NavAgentTargetPauseAndResume`, `NavGridStartBakesLateAndSpawned`, `NavSerializationAndFingerprintEdgeCases`, `FormationMembersKeepUpWhileWalking` |
| 120 | M21: prefab links, overrides, apply / revert, nesting | 9 | `PrefabLinkSerialization`, `PrefabInstantiateLinked`, `PrefabLegacyLinkMenuScene`, `PrefabOverrideDetection`, `PrefabApplyPropagate`, `PrefabRevertLevels`, `PrefabSyncStructural`, `PrefabNested`, `PrefabUndoFileEdits` |
| 121 | M21 batch B: prefab mode, references, versions, recovery | 6 | `EntityRefVisitorCoverage`, `PrefabEntityRefRemap`, `EntitySetVersionMigration`, `EntitySetPartialLoad`, `SceneSalvage`, `PrefabStageRoundTrip` |
| 122 | serialization fuzz (411) | 10 | `SerializationFuzz`, `PrefabCacheReads`, `LargeSceneLinkedInstances`, `PrefabVariants`, `PrefabPlacementAndThumbs`, `PrefabHotReloadSync`, `ClipboardPrefabFormat`, `PrefabExportValidation`, `PrefabFieldLabelsExist`, `PrefabDamagedFileNotWritten` |
| 123 | physics determinism (Box2D, Jolt) | 2 | `PhysicsDeterminism2D`, `PhysicsDeterminism3D` |
| 124 | editor Play copy (the exported game's order) | 1 | `PlayCopyRunsScriptsInFileOrder` |
| 125 | script hot reload (in-process) | 3 | `ScriptReloadKeepsFieldValues`, `StaleModuleCopiesRemoved`, `DeferredDestroyOnceEachAndLinear` |
| 126 | M25: optimization (tasks 481-487) | 16 | `BatchOrderGroupsDrawsThatDoNotOverlap`, `BatchOrderKeepsOverlappingDrawsInOrder`, `BatchOrderRandomScenesKeepEveryOverlap`, `WorldTransformCacheFollowsChanges`, `SteadyFramesMakeNoHeapAllocations`, `FontKerningIsOptInAndNarrowsPairs`, `FontAtlasCacheGivesTheSameAtlas`, `AsyncLoaderWorkOnWorkersFinishOnMainThread`, `AsyncLoaderImmediateFinishesLoadsAndRestoresMode`, `PrimitiveLODForSmallCurvedMeshes`, `AsyncLoaderStartupModeFollowsCaptures`, `ModelLoadedOnWorkerKeepsGpuWorkForMainThread`, `ShaderCacheStoresProgramBinaries`, `ShadowCastersCulledByWhatTheCameraSees`, `LargeParticleSystemSteppedInPiecesMatchesSerial`, `AnimatorVisibilityMarkedInParallelMatchesSerial` |
| 127 | M25: start-up time, editor font icons, translations (tasks 495, 496, 499) | 4 | `StartupTimerParsesProcessStartTime`, `IconGlyphRangesCoverIconsHeader`, `EditorTranslationKeysComplete`, `EditorNoRawUzbekLiterals` |
| 128 | WorldCheckpointTests.cpp | 10 | `WorldCheckpointAtomicRoundtripReplacementAndEmptyCell`, `WorldCheckpointRejectsBrokenFilesWithoutChangingState`, `WorldCheckpointRejectsDuplicateKeysCyclesAndUnknownParents`, `WorldCheckpointRejectsSymlinkAndFIFOWritesPreservePreviousFile`, `WorldGridAuthoringCreatesPortable2DAnd3DBundles`, `WorldGridAuthoringBoundsFailureAndSymlinkProtection`, `WorldGridAuthoringClonesTemplateHierarchyAndRemapsReferences`, `WorldGridAuthoringTemplateFailureLeavesNoBundle`, `WorldCheckpointWriterIsBoundedAndOwnsImmutableSnapshot`, `WorldCheckpointWriterJoinsAndReportsIOFailure` |
| 129 | ResidencyTerrainNavTests.cpp | 12 | `ResidencyLRUPreservesLiveOwnersAndLoading`, `ResidencyLRUTouchAndBudgetSemantics`, `ResidencyPressureHysteresisAndDisabledSignals`, `NavMosaicCrossTilePathAndUnloadHole`, `NavMosaicHeightCostCarveAndClimb`, `NavMosaicRejectsOverlapsAlignmentAndHugeBoundsTransactionally`, `NavMosaicNegativeOriginAndInputOrder`, `WorldTerrainContinuousGeneratorAndRangeRollback`, `WorldTerrainStitchUnequalResolutionAllDirections`, `WorldTerrainStitchFailureIsAtomic`, `WorldTerrainBorderLODAndSerialization`, `ResidencyBytePlannerDoesNotLoseOverflowDebt` |
| 130 | WorldStreamingTests.cpp | 59 | `WorldStreamingDefinitionPolicyRoundtripAndTransactionalValidation`, `WorldStreamingCameraHysteresisOffsetUnloadAndReturn`, `WorldStreamingDependenciesLayersAndDeclaredAdmissionBudget`, `WorldStreamingBulkImportStartsScriptsAfterAllEntitiesExist`, `WorldStreamingSharedPhysics2DAnd3DThenBodyUnload`, `WorldStreamingEntityQuotaCollisionFailureRetryAndSingleLoad`, `WorldStreamingXYSelectionAndRapidCancellation`, `WorldStreamingExportReachabilityAndProjectSettingRoundtrip`, `WorldStreamingTinyBudgetStillProgressesAtManifestCapacity`, `WorldStreamingObservedMemoryGateDoesNotDropLiveCells`, `WorldStreamingNavigationAcrossLoadedCellsAndRetirement`, `WorldStreamingResidencyPolicyRoundTripAndBounds`, `WorldStreamingPressureRetiresCellsBeforeNewAdmission`, `WorldStreamingTerrainSeamSharesPhysicalFloor`, `WorldStreamingReloadChangedEntityCountResetsResolutionCursor`, `WorldStreamingManualAndAutomaticTrimShareFrameAllowance`, `WorldCheckpointRuntimeCapturesEditsDeletesAndAdoptedSubtrees`, `WorldCheckpointFailedCaptureKeepsLiveCellAndPreviousFile`, `WorldCheckpointRestorePreflightAndOwnershipAreTransactional`, `WorldCheckpointSessionRestartRestoresColdCells`, `WorldCheckpointCommandQueueIsBoundedFIFOAndFrameLimited`, `WorldCheckpointCaptureRequiresExplicitChildOwnership`, `WorldCheckpointComponentPreflightRejectsBadSnapshotBeforeRetirement`, `WorldStreamingCellPreviewDoesNotStartTheWholeConfiguredWorld`, `WorldStreamingSparseAtlasLinksRoundTripAndUnload`, `WorldStreamingNavigationLinkDefinitionRejectsMalformedTransactionally`, `WorldNavAgentWalksPortalsAndRequiresExactCompletionToken`, `WorldNavAgentStaleAndCancelledRequestsCannotCompleteLater`, `WorldNavAgentUnloadStopsAndReturnReplans`, `WorldNavAgentControllerCannotWalkThroughPhysicalWallOrOccupiedExit`, `WorldNavAgentSerializationCopyAndRuntimeReset`, `WorldNavAgentPeriodicRepathDoesNotTurnBackWithinStartCell`, `WorldNavAgentVisualGraphDrivesDestinationAndPortalCompletion`, `WorldNavAgentGraphOverflowDoesNotChangeNavigationMode`, `WorldAsyncNavigationPolicyRoundTripAndLimits`, `WorldAsyncNavigationPendingStopsUntilOwnerPublishesAndStillTraverses`, `WorldAsyncNavigationRejectsChangedGoalAndCrossedStartCell`, `WorldAsyncNavigationCancelDestroyAndTopologyRetirePendingJobs`, `WorldAsyncNavigationBackpressureEventuallyServesEveryAgent`, `WorldAsyncNavigationControllerWallAndExitOccupancyRemainAuthoritative`, `WorldAsyncNavigationDoesNotDeliverToRecreatedUUID`, `WorldAsyncNavigationGraphPendingReflectsOwnerQueue`, `WorldAsyncNavigationEmptyAtlasFailsAndReturnResumes`, `WorldCrowdSettingsSerializeAndRuntimeReset`, `WorldSmoothingUsesExistingAgentSetting`, `WorldCrowdHeadOnAgentsReachGoalsIn2DAnd3D`, `WorldMovementGuardCannotWalkThroughMissingTiles`, `WorldAsyncSmoothingChangeRejectsOldOptions`, `WorldCarvingPublishesBeforeAgentMotionIn2DAnd3D`, `WorldMosaicBindingCarvesSourceTilesWithoutRebuildFeedback`, `WorldObstacleDisableDestroyAndRetainedSnapshotsAreCoherent`, `WorldObstacleInvalidatesQueuedQueryBeforeDelivery`, `WorldRecoveryPhysicalControllerStallHasBoundedRetries`, `WorldRecoveryPartialEndExhaustsAndNewTopologyResumesIntent`, `WorldRecoverySettingsSerializeCopyAndClampWithoutRuntimeState`, `WorldObstacleAtPortalExitRevokesCompletionBeforeNextTick`, `WorldRecoveryGraphReportsExhaustionAndRetriesExplicitly`, `AIGameplayWorldQueryWaitAndPortalHandshakeArePreservedAndCancelled`, `GameSaveWorldStreamingRestoreCancelsQueriesAndInvalidatesPortalTickets` |
| 131 | WorldAssetStreamingTests.cpp | 15 | `AsyncLoaderUploadBudgetBoundsBytesAndCallsPerUpdateInOrder`, `AsyncLoaderUploadBudgetCutsSteppedUploadsAndIsolatesOversizeUnits`, `AsyncLoaderUploadBudgetIsIgnoredByExplicitWaitsAndCancelledPieces`, `WorldAssetStagerDecodesOffTheOwnerThreadWithinByteBudgets`, `WorldAssetStagerFileBudgetsMissingAndCorruptFiles`, `WorldAssetStagerRapidSubmitAndCancelKeepsReservationsBounded`, `WorldAssetStagerModelsMaterialsAndUploadUnitLimit`, `WorldStreamingAssetBudgetPolicyRoundTripDefaultsAndBounds`, `WorldStreamingAssetBudgetActivatesAssetFreeCellsAndRestoresUploadBudget`, `WorldStreamingResidencyLimitEvictsFarthestCellsAndReturnsAfterCooldown`, `WorldStreamingResidencyLimitWithoutEvictionOnlyPausesAdmission`, `WorldStreamingResidencyLimitKeepsDependenciesOfRemainingCells`, `WorldTourEvaluatesByFrameNumberWithJumpsAndRoundTrips`, `WorldTourReportPercentilesAndRunnerRecordsEveryTourFrameOnce`, `WorldGalleryDemoSavesReloadsAndExportsItsAssetClosure` |
| 132 | WorldCellLoaderTests.cpp | 11 | `WorldCellLoaderDependencyClosureBackpressureAndRealSceneBuild`, `WorldCellLoaderCancellationDropsOldGenerationAndRetriesExplicitly`, `WorldCellLoaderInvalidRequestsPreserveWantedAndDependencyFailurePropagates`, `WorldCellLoaderRejectsUntrustedSceneStructuresAndLimits`, `WorldCellLoaderRejectsSymlinksAndSpecialFiles`, `WorldCellLoaderPinsAssetRootAgainstPathReplacement`, `WorldCellLoaderRapidCameraChangesKeepReservationsBounded`, `WorldCellLoaderValidatesConfigurationAndOwnerThread`, `WorldCellLoaderStagesReal2DAnd3DScenesWithoutActivation`, `WorldCellOverrideIsImmutableUntilNextGeneration`, `WorldCellOverrideBoundsMalformedAndMissingAuthoredSource` |
| 133 | WorldManifestTests.cpp | 5 | `WorldManifestCanonicalRoundtripBoundsAndOrder`, `WorldManifestRejectsInvalidDependenciesAndLayers`, `WorldManifestUntrustedInputIsBoundedAndTransactional`, `WorldManifestAtomicFilesSafeResolutionAndWorkerParse`, `WorldManifestCapacityAndDeepDependencyChain` |
| 134 | PackedFoliageTests.cpp | 4 | `PackedFoliageDistanceFrustumAndHysteresis`, `PackedFoliageValidationAndDeterministicPlacement`, `PackedFoliageSaveLoadCopyAndBounds`, `PackedFoliageProjectsWithoutBreakingRootTransform` |
| 135 | ModuleABITests.cpp | 6 | `ModuleABIValidRecordAndFingerprintMismatch`, `ModuleABIRejectsLayoutAndFlagMismatch`, `ModuleABIRejectsMissingOrMalformedRecord`, `ModuleABIRejectsELFOverflowTruncationAndWrongArchitecture`, `ModuleABIRejectsMalformedSectionRanges`, `ModuleABIMutatedRecordsAreBounded` |
| 136 | TerrainFoliageTests.cpp | 5 | `TerrainFoliageReprojectsTaggedRootsAndKeepsHierarchy`, `TerrainFoliageRegionOffsetBoundsAndEditorLocks`, `TerrainFoliageAlignsWorldNormalUnderNonuniformParent`, `TerrainFoliageRejectsInvalidRuntimeAndSingularInputs`, `TerrainFoliageReprojectSerializesAndCopiesIndependently` |
| 137 | CodeDocumentTests.cpp | 9 | `CodeDocumentPreservesUnicodeBomCrlfAndFinalNewline`, `CodeDocumentConflictsNeverSilentlyOverwrite`, `CodeDocumentRecoveryKeepsOriginalConflictBaseline`, `CodeDocumentUndoToSavedRemovesStaleRecovery`, `CodeDocumentRejectsBinaryInvalidUtf8AndOversizedEdits`, `CodeDocumentSearchReplaceIsNonOverlappingAndBounded`, `CodeDocumentProjectBoundaryAndSymlinkEscape`, `CodeDocumentFailedAtomicSaveKeepsDraft`, `CodeDocumentDamagedRecoveryDoesNotBlockSource` |
| 138 | VisualGraphTests.cpp | 29 | `VisualGraphTypedRoundTrip`, `VisualGraphRejectsInvalidPinsAndCycles`, `VisualGraphVariableTypesChecked`, `VisualGraphForLoopReadsFreshVariables`, `VisualGraphDelayKeepsLoopContinuation`, `VisualGraphSequenceWaitsForDelayedBranch`, `VisualGraphBranchChoosesOnePath`, `VisualGraphInfiniteExecutionIsBounded`, `VisualGraphNonFiniteMathFailsAtNode`, `VisualGraphInstancesAndOverridesAreIsolated`, `VisualGraphDebugPauseStepAndContinue`, `VisualGraphEventPayloadDrivesDataPins`, `VisualGraphVectorBridgeReceivesScaledDelta`, `VisualGraphRejectsUnsupportedFormats`, `VisualGraphBuiltinCannotBeUnregistered`, `VisualGraphNativeHostLifecycleWithoutCppModule`, `VisualGraphFunctionsResumeAfterDelayAndReturnToCaller`, `VisualGraphFunctionNamesAreValidated`, `VisualGraphWhileReevaluatesConditionAndBreakCompletes`, `VisualGraphWritesAndReadsReflectedCppFields`, `VisualGraphPendingWorkIsBoundedAndStopCancelsDelay`, `VisualGraphExplicitNullEntityDoesNotMoveSelf`, `VisualGraphCatalogHasTypedDefaultsAndUniquePins`, `VisualGraphBridgeCanReturnSeveralTypedOutputs`, `VisualGraphSampleAssetsRemainReachableInExport`, `VisualGraphTargetsEntitiesInOtherLoadedScenes`, `VisualGraphTraversalTokensKeepFullUnsignedPrecisionAndSuccessOutput`, `VisualGraphWorldDestinationFailureAndCancelReturnBooleans`, `BlueprintWorkshopRunsTypedDelayedCollectionsInRealScenes` |
| 139 | TerrainTests.cpp | 10 | `TerrainBrushesAndNormalizedPaint`, `TerrainWeightValidationDoesNotDriftAcrossFrames`, `TerrainGeometryRaycastAndSeeds`, `TerrainSerializationCopyAndHeightmap`, `TerrainPBRSerializationAndCopy`, `TerrainPBRInvalidValuesAreSanitized`, `TerrainLegacyLayersKeepPBRDefaults`, `TerrainPBRReferencesFollowRenameAndExportReachability`, `TerrainJoltCollisionAndPlayCopy`, `TerrainSceneWriteFailurePreservesOriginal` |
| 140 | terrain patch geometry and distance LOD | 8 | `TerrainPatchCoverageAndWinding`, `TerrainDistanceLODAndTransformInvariance`, `TerrainMixedLODEdgesRemainClosed`, `TerrainLODHysteresisAndOrthographicQuality`, `TerrainBoundaryNormalsAndInvalidPatches`, `TerrainCollisionRemainsFullResolutionAcrossLOD`, `TerrainStitchedCornersPreserveAreaAndTopology`, `TerrainPartialPatchesBalanceAndRemainManifold` |
| 141 | large 2D tilemap hierarchical visibility | 3 | `TilemapHierarchyMatchesConservativeReference`, `TilemapLargeMapPrunesWithoutFullScan`, `TilemapFullViewAndNoFrustumKeepRowOrder` |
| 142 | audit regression tests | 6 | `ProcessLiteralArgumentsAndOutput`, `ProcessDrainsOutputAndReapsOnCallbackFailure`, `IncompleteShaderFilesDoNotCrash`, `UndoMemoryBudgetAndSavedState`, `UndoBudgetIncludesFileEditsAndRewrite`, `AtomicSavePermissionFailureKeepsOriginal` |
| 143 | M16 Editor II | 15 | `ConsoleFilterAndCollapse`, `GameViewResolutionLayout`, `EditorPreferencesSettings`, `ShortcutRegistryLookup`, `RecentFilesMRU`, `EditorAutosaveRecovery`, `UndoHistoryNamingAndJumps`, `EditorLayoutLibraryFiles`, `SceneViewAxesAndCamera`, `EntityLabelsLayoutAndFade`, `SceneStatisticsCounting`, `ExternalEditorCommands`, `CompilerOutputParsing`, `IdleThrottleDecision`, `EditorIIPureLogic` |
<!-- test-catalog:end -->


## Terrain map regression

`Tests/TerrainTests.cpp`: `TerrainBrushesAndNormalizedPaint`, `TerrainGeometryRaycastAndSeeds`, `TerrainSerializationCopyAndHeightmap`, `TerrainJoltCollisionAndPlayCopy`, `TerrainSceneWriteFailurePreservesOriginal`. They check the brush limits, the CCW mesh, a raycast that matches the surface, independent copies / caches, saving, R16 and the Jolt collision.


SDK and profiling checks: `ctest --test-dir build-review -R 'ModuleABILoad|SDKManifest' --output-on-failure`; in a GL context `ZaminGPUProfilerTests` and `ZaminTerrainSurfaceTests`. `Tests/sdk_manifest_test.py --build build-review` checks relocated SDK headers and a compiler mismatch without changing the engine source. `ZAMIN_EDITOR_TERRAIN=foliage-test` runs the reproject / Undo / Redo check on a copy of the test project only.


The extra GL oracle of packed foliage: `ZaminFoliageRenderTests --require-gpu` (GL 4.3+) and `MESA_GL_VERSION_OVERRIDE=4.1 MESA_GLSL_VERSION_OVERRIDE=410 ZaminFoliageRenderTests` (the CPU fallback). Run each under `LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a`. 4096 instances and 5 cameras: CPU/GPU count / pixels, LOD hysteresis, the input upload cache, the multi-part indirect command, the backend switch, shadow / picking / wire and the rejection of wrong winding / LOD. [The foliage sample](../Projects/FoliageWorkshop/README.md) and [the limits](Terrain.md#packed-foliage-and-two-model-lod).


`SanitizerSharedLink` (Linux + Clang/compiler-rt) checks the production CMake shared-library policy
in a small separate project: the DSO / consumer link with Clang ASan, UBSan and both,
a normal start, the detection of a real signed overflow and the rejection of an
application symbol that a plain Clang build lacks. Skipped without Clang/compiler-rt; `ctest -R SanitizerSharedLink`
or `python3 Tests/sanitizer_link_test.py --compiler /usr/bin/clang++-18`.


### World streaming package regression

`ZAMIN_BUILD_DIR=build-review ctest --test-dir build-review -R WorldStreamingRuntime --output-on-failure`
or `python3 Tests/world_streaming_test.py --build build-review --out build-review/WorldPackage/Runtime`:
the WorldWorkshop3D/2D Player camera path, unloading into an empty region and returning, the admission limit, 0
failed cells, a real framebuffer screenshot and the editor's world panel. The test has the `gui;world` label,
separate from the usual CTest run without a GUI. The CPU suite checks shared 2D/3D physics, the bulk import
lifecycle, radius / hysteresis, dependency / declared admission, UUID collision / retry, Stop / Single, the export
closure and the progress of a very small budget on a manifest of 4096 cells.


### World asset streaming: budget, limit and demo

`Tests/WorldAssetStreamingTests.cpp` (`ZaminTests "AsyncLoaderUpload*" "WorldAssetStager*" "WorldStreamingAsset*"
"WorldStreamingResidencyLimit*" "WorldTour*" "WorldGalleryDemo*"`, without GL): the `AsyncLoader` upload budget
(bytes / calls, queue order, chunked upload, a unit larger than the budget on its own), `WorldAssetStager`
(decoding on a worker, rejection from the header, the file / cell / total budget, a missing / broken file, quick
submit / cancel, model / material), the policy round trip and the defaults of an old file, evicting the farthest
cell on a limit and the dependency order, `.ztour` evaluation / round trip / invalid files, the report's
percentiles, the WorldGallery3D save / reload / export plan.

`ctest -R AssetResidencyGPU` (`ZaminResidencyGPUTests`, real GL): the byte budget of a row-by-row upload and its
equality with a texture uploaded without a budget (the worker's mip chain differs by 0 from the driver's
`glGenerateMipmap`), a half-uploaded texture staying invisible, forced completion, the GPU allocation being
returned on cancel; a real cell (sprite + OBJ + `.zmat`): 0 decode / parse on the main thread, the frame budget,
returning, cancelling, a fast camera, the release budget, a file over the budget and Retry; on the path with
`AssetStreaming` off the counters are > 0.

`ctest -R WorldAssetStreaming` (`gui;world;slow`, three stages, each on its own:
`python3 Tests/world_asset_streaming_test.py --stage budget|residency|demo [--out DIR] [--keep DIR]`) —
the real Player, the `ZAMIN_WORLD_TOUR` camera path and the `ZAMIN_WORLD_REPORT` report:

- **budget** — a generated heavy world (24 cells, 2048 px textures, a model of 5184 triangles, 640×360) with the
  budget and on the `--legacy` (main thread) path. Hard gates: 0 decode / parse on the main thread; upload bytes
  per frame ≤ `MaxAssetUploadBytesPerFrame`, calls ≤ the limit, 0 units larger than the budget; staging ≤
  `MaxStagedAssetBytes`; **the streaming work of one frame (world update + upload) ≤ 30 ms**
  (`ZAMIN_WORLD_HARD_MS`) and less than half of legacy's, while legacy has to be above 30 ms (which proves that the
  gate fails without the change); none of the frames 40 ms longer than the median is tied to streaming work
  (> 5 ms), p99 − p50 ≤ 40 ms; with a cell budget of 8 MiB the cells are rejected by name and nothing is loaded.
- **residency** — a two-lap tour without limits (a fast camera, an empty region, returning: the GPU / RSS in the
  empty region and the lap peaks do not grow), the requested GPU limit (never reached, the world keeps working),
  the RSS limit (pressure, returning to the OS / eviction, a bounded overshoot) and a broken world (a missing cell,
  broken cell JSON, a broken and a missing texture, a broken model: exactly 2 failed cells, the rest work).
- **demo** — `Projects/WorldGallery3D` is byte for byte the generator's output; two tours have the same camera path
  and the same active cells at the rest points; the editor's world panel; export, moving the folder and the same
  tour in the export.

Frame time is measured on Mesa llvmpipe: this is a correctness and budget check, not an estimate of the speed of a
physical GPU. A single long frame (up to 300 ms) is the renderer compiling a shader variant used for the first
time — the streaming work in that frame is 0.2 ms; the report counts it separately.

### Residency / world terrain / grid mosaic package

`ZaminTests --filter "Residency:NavMosaic:WorldTerrain:WorldStreaming"` checks the LRU and the live / loading pin,
byte overflow, pressure hysteresis, the transactional seam, a cross-cell path and an unloaded hole, the observed
memory gate, a stale mosaic disappearing on a source topology error, the explicit grid choice and the world close
lifecycle. `xvfb-run -a build/bin/ZaminResidencyGPUTests` checks texture / model / material / mesh ownership,
deferred upload, async load and 32 eviction cycles with real GPU allocation accounting. In CI it runs on normal GL
4.1 and in the ASan smoke. `python3 Tests/world_streaming_test.py --build build --out world-streaming` saves the
screenshots / logs of the camera tour, the cross-cell path, the residency metrics, the world panel and the terrain
pair edit Undo / Redo in the two enlarged demos. The headless `WorldModuleLocations` CTest checks the default,
isolated and custom module output paths.

### World checkpoint and authoring

`ZaminTests --filter "WorldCheckpoint:WorldGridAuthoring:WorldCellOverride:WorldStreamingCellPreview"`:
checks of the atomic snapshot round trip, the immutable writer, exit join, IO failure, UUID / hierarchy and
malformed preflight, parent / child ownership, a deleted / adopted subtree, the origin offset, cold-session restore,
the bounded FIFO command, override generation, template clone / remap, no-overwrite staging cleanup and the anchor /
cell preview. `python3 Tests/world_checkpoint_test.py --build build --out world-checkpoint` saves the 2D/3D Player
and editor workflows, the checkpoint JSON, a screenshot and a copy of the generated export project. This is the GUI
CTest `WorldCheckpointRuntime`; it also runs in the normal GCC/Clang CI matrix.

### Sparse atlas and traversal workflow

`ZaminTests --filter "NavAtlas:NavRouteFollower:WorldStreamingSparse:WorldStreamingNavigationLink"`
checks loaded-only allocation, the dense path cost oracle, random cost / obstacle maps, the height / climb / corner
rules, directed / disabled / missing / carved links, immutable copies, a deterministic parallel query, the expansion
cap, float precision / overflow, explicit traversal completion and the stale route stop.
The world fixture covers the `.zworld` link round trip / malformed input and far-apart tile unload / return.
`python3 Tests/world_navigation_test.py --build build --out navigation` checks, in the real 2D/3D Player over
420 frames, the portal completion, disable, stale stop, empty unload and return replan oracles,
then the route preview and the framebuffer PNG in each editor. The GUI CTest is named
`WorldNavigationRuntime`; the GCC/Clang CI uses the same runner. `--no-editor` is for the Player part.
In the native export test `ZAMIN_WORLD_NAVIGATION_TEST=1` and `--frames 420` run the same
workflow in the exported Player too. The ASan/UBSan leaf probe is not to be read as a sanitizer result for the whole
engine.

### NavAgent / controller / Visual Graph integration

`ZaminTests --filter "WorldNavAgent:VisualGraphTraversal:VisualGraphWorldDestination"` checks, on a real world
fixture, distant tiles, portal entry and the completion token, cancel / stale / retry, scene ownership,
a controller wall and an occupied exit, BaseOffset, runtime reset / copy / serialize, the periodic replan and the
exact goal, the built-in graph destination / completion, String uint64 precision and the Success outputs.
`python3 Tests/world_agent_test.py --build build --out agents` checks, with ZAMIN_CAPTURE_FPS=30, three real
traversals / arrivals, disable / unload / return over 420 frames in each 2D/3D Player and a 120-frame graph
editor screenshot. The CTest `WorldAgentRuntime` is wired into the normal Linux GCC/Clang CI.
For a native export: `ZAMIN_WORLD_AGENT_TEST=1 ZAMIN_CAPTURE_FPS=30 Tests/export_test.sh ... --frames 420`.
The targeted sanitizer probe is linked with the agent / VM TUs and the tests; the shared engine is a normal
build in it, so it is not to be read as a full engine sanitizer run.

### Async navigation queue

`NavQueryQueueTests.cpp`: independent start/publication budgets, queued replacement, unique snapshot accounting,
capacity/byte limits, oversized result failure, expansion/point cap, active cancellation, joined shutdown,
a pre-cancelled path and mixed replacement cycles. The pure queue/atlas/grid ASan/UBSan probe is linked without the
engine. `WorldAsyncNavigation*`: policy, pending motion, actor destruction/cancel, same UUID/new generation, changed
goal, crossed start cell, backpressure fairness, controller wall/occupied exit, graph pending state, empty
atlas/return. `Tests/world_agent_test.py` checks three delivered queries and a real portal arrival in the async
demo; the editor attaches the graph to the actor instance. Worker scratch is separate from the payload budgets; no
TSan result is claimed.

World crowd runtime: `python3 Tests/world_crowd_test.py --build build --out build/WorldCrowdRuntime`.
Each 2D/3D Player passes, in 450 frames, the oracle of a four-way crossing, two destination stages, Stop / reverse,
minimal separation and real VisualGraph telemetry. The editor takes, in 130 frames,
a screenshot of the 5-node / 6-link CrowdMonitor graph and of the scene instance (`--no-editor`: Player).
`ZAMIN_WORLD_CROWD_TEST=1 ZAMIN_CAPTURE_FPS=30` runs the same oracle in a native export too.
The leaf tests check crowd snapshot determinism, head-on / crossing, cap / invalid input and the sparse segment /
weighted smoothing / portal / floor / budget rules. The scene regressions check a real 2D/3D
controller, serialization / reset, a change of the async option and the unloaded gap movement guard.

The CI dependency installer `Tests/install_linux_dependencies.sh` checks the DNS of the Azure Ubuntu mirror
within a bounded time; on a DNS or apt error it switches to the signed official archive mirror.
Download retries / timeouts are bounded, and if the fallback fails too the job ends with an error.
This does not replace the engine tests; when the dependency stage has not passed, the C++ checks have not run yet.

`Tests/world_recovery_test.py --build build --out build/WorldRecoveryRuntime`: the 2D/3D Player for 420 frames,
a live blocker, bounded exhaustion, stable source carving (no mosaic feedback), graph telemetry,
and arrival after the door opens without giving the destination again. The editor takes, in 160 frames, the
8-node / 9-link RecoveryMonitor graph and the scene instance. `ZAMIN_WORLD_RECOVERY_TEST=1 ZAMIN_CAPTURE_FPS=30`
runs the oracle in a native export too. The unit regressions check same-frame publication, a queued stale
result, portal-exit revocation, a physical controller stall and the serialize / copy reset.

The installer regression `Tests/install_linux_dependencies_test.py` does not run the real apt / sudo:
it checks DNS fail-fast, the mirror fallback, the bounded retry and the timeout arguments of the whole apt command
with mocks. Because the HTTP timeout did not bound the DNS wait per package,
the whole update / install is also limited to 120 / 240 seconds (+10 seconds of kill grace).

With `--editor-only` the recovery runner checks the editor diagnostics on their own, without repeating a Player run
that already passed (not to be combined with `--no-editor`). In the installer, when all the packages are already
installed on the runner, the DNS / apt stage is skipped; the mock regression covers this case too.
