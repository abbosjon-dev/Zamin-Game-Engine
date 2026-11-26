# Code coverage (gcov)

Shows which part of the engine code the tests really run: which module is well tested, which files no test touches
at all. The number is not a goal in itself - it is there to find the places the tests do not reach.

## Running

```bash
Tests/coverage.sh                    # build-cov (coverage build), ZaminTests, report: build-cov/Coverage
Tests/coverage.sh --gui              # + ZaminPlayer (3 sample games, headless) and the editor smoke test (xvfb)
Tests/coverage.sh --html             # + an HTML report per file (when gcovr is installed: pipx install gcovr)
Tests/coverage.sh --filter "Prefab*" # only some tests (ZaminTests --filter)
Tests/coverage.sh --min-engine 55    # exits with code 1 when the engine line coverage is below 55 %
Tests/coverage.sh --report-only      # only the report, from the counts the last run left
Tests/coverage.sh --clean --out DIR  # the report into DIR, the build folder (~1 GB) is deleted at the end
```

What the script does:

1. **A coverage build** - in a separate folder (`build-cov`, the main `build/` is not touched): `-DZAMIN_COVERAGE=ON`
   (`cmake/Coverage.cmake`, the `coverage` preset in `CMakePresets.json`). Only our targets (engine, editor,
   player, tests, benchmarks, the sample game modules - `zamin_target_settings`) are compiled with `--coverage -O0
   -fprofile-update=atomic`; the vendor libraries (Box2D, Jolt, ImGui, ...) are not, so the report is only about our
   code. `-O0` - the line counts match the source (the particle files too, without the `-O2` they get in a normal
   Debug build); atomic counts - the worker threads of the job system run engine code too. As in the sanitizer
   build, the game modules are written to `build-cov/GameModules` and the build's own programs (`build-cov/bin`)
   load them from there; scripts the coverage editor builds (hot reload) also go to that folder, with counts, through
   `build-cov/ZaminConfig.cmake` - the module of the normal build in `Binaries` is not touched.
2. On every run the old `.gcda` counts are deleted, then **`ZaminTests`** (all the tests) runs;
   with `--gui` the player and the editor too.
3. **`Tests/coverage_report.py`** (standard library only) runs `gcov --json-format` for every `.gcda` in parallel,
   merges the results of the same file from all translation units (a line of a header file is executed if some `.cpp`
   executed it), drops the files outside the repository and in `vendor/` and the build folders, and sums up **by
   module**: the engine folders (`Zamin/Core`, `Zamin/Scene`, `Zamin/Renderer`, ...), `Editor`, `Editor/Panels`,
   `Player`, the sample games (`Sample/<project>`). The tests' own code only with `--include-tests`.
4. The result: `coverage-summary.md` (a Markdown table - CI puts it on the job page), `coverage-summary.html` (the same
   table with bars, one page - no gcovr needed), `coverage.json` (modules, files, the lines of each file that were not
   executed), and with `--html` `html/index.html` (gcovr: every file line by line, branches too).

For a Clang build: `Tests/coverage.sh --compiler clang` (`llvm-cov gcov` instead of gcov).

**CI:** the `coverage` job in `.github/workflows/linux.yml` runs `Tests/coverage.sh --html --out coverage-report`,
writes the table to the job page and uploads the report as the `coverage-report` artifact.

## Results (2026-11-08)

Environment: GCC 13.3, Debug + `--coverage -O0`, 4 vCPU. Two measurements: **unit tests** (`Tests/coverage.sh`, the
`coverage` job in CI) - `ZaminTests` only (627 tests, all passed), the editor / player are not built; **with
`--gui`** - the unit tests + ZaminPlayer (Platformer2D, Showcase3D, Adventure3D; 120 frames each, headless) + the
editor smoke test (Showcase3D 221 steps, Platformer2D 263 steps, 0 errors). A line is an executable source line counted
once (a header used in several `.cpp` files too), a function is one function in the source.

| Module | Files | Lines | Unit tests: lines / functions | With `--gui`: lines / functions |
|---|---:|---:|---:|---:|
| Zamin/AI | 21 | 2 422 | 88.6 % / 93.5 % | 89.2 % / 92.6 % |
| Zamin/Asset | 18 | 2 657 | 91.4 % / 97.4 % | 92.5 % / 99.6 % |
| Zamin/Audio | 8 | 2 077 | 93.6 % / 95.1 % | 93.7 % / 95.8 % |
| Zamin/Core | 48 | 3 953 | 81.6 % / 88.6 % | 87.5 % / 94.3 % |
| Zamin/Debug | 3 | 848 | 72.3 % / 91.8 % | 88.3 % / 98.4 % |
| Zamin/Editor | 31 | 2 967 | 93.3 % / 97.8 % | 93.9 % / 99.4 % |
| Zamin/Events | 1 | 49 | 18.8 % / 13.6 % | 49.0 % / 44.4 % |
| Zamin/ImGui | 3 | 197 | 0.0 % / 0.0 % | 89.3 % / 81.8 % |
| Zamin/Localization | 2 | 653 | 91.1 % / 97.8 % | 91.4 % / 97.8 % |
| Zamin/Math | 19 | 1 986 | 95.2 % / 97.7 % | 94.8 % / 95.8 % |
| Zamin/Physics | 14 | 3 617 | 92.4 % / 95.2 % | 92.5 % / 95.5 % |
| Zamin/Platform | 10 | 1 423 | 8.2 % / 11.3 % | 59.0 % / 71.0 % |
| Zamin/Project | 16 | 3 236 | 93.8 % / 99.3 % | 93.9 % / 99.3 % |
| Zamin/Renderer | 51 | 6 847 | 56.9 % / 67.8 % | 85.3 % / 90.4 % |
| Zamin/Scene | 60 | 23 463 | 87.7 % / 93.2 % | 90.6 % / 95.2 % |
| Zamin/Scripting | 9 | 1 414 | 67.8 % / 64.7 % | 69.2 % / 66.4 % |
| Zamin/UI | 9 | 956 | 92.5 % / 99.1 % | 94.8 % / 100.0 % |
| Zamin/Utils | 6 | 890 | 93.3 % / 99.2 % | 93.3 % / 99.2 % |
| Editor | 38 | 17 268 | - (not built) | 46.0 % / 56.4 % |
| Editor/Panels | 36 | 21 009 | - (not built) | 41.6 % / 58.6 % |
| Player | 1 | 862 | - (not built) | 33.5 % / 50.0 % |
| Sample/Adventure3D | 3 | 308 | - (not built) | 60.1 % / 61.1 % |
| Sample/Platformer2D | 14 | 1 126 | - (not built) | 41.5 % / 40.9 % |
| Sample/Showcase3D | 5 | 1 174 | - (not built) | 5.1 % / 8.1 % |
| **Engine (Zamin) total** | 329 | 59 655 | **82.6 %** / 86.3 % | **89.3 %** / 92.4 % |
| **Editor total** | 74 | 38 277 | - | **43.6 %** / 57.5 % |
| **Player** | 1 | 862 | - | **33.5 %** / 50.0 % |
| **Sample games total** | 22 | 2 608 | - | **27.3 %** / 29.3 % |
| **All** | 426 | 101 402 | **82.6 %** / 86.3 % | **70.0 %** / 82.6 % |

In total: **82.6 % of the engine lines with the unit tests alone**, **89.3 %** when the player and the editor run;
with `--gui` the whole code (engine + editor + player + sample games, 101 402 lines) - **70.0 %**.

### What it shows

- **Well covered** (above 90 %): Scene (scenes, serialization, prefabs - the largest module, 23 463 lines),
  Physics, Audio, Asset, Project, Math, UI, Utils, Localization. They are mostly checked by the unit tests.
- **Covered only with the GUI:** Renderer (56.9 % in the unit tests - the tests run without a GPU; 85.3 % with the
  player and the editor), Platform (OpenGL, the GLFW window: 8.2 % -> 59.0 %), the ImGui layer (0 -> 89.3 %). The
  rendering regression test (`ZaminRenderTests`) is not part of this report - it runs this code too.
- **Poorly covered:** Scripting (69 %) - many helper functions of the `ScriptableEntity` API are called only from
  scripts and the tests use part of them; Events (49 %) - small, window events only; the editor (43.6 %) - the smoke
  test opens every window but does not press many buttons / menu actions (for example `AnimationTimelinePanel` 10 %,
  `EditorPrefabs.cpp` 3.9 %, `UICurves.cpp` 0 %). The sample games: Showcase3D 5.1 % - the player plays only the start
  scene for 120 frames, the scripts of the other showcase scenes do not run.
- **A flaw found by coverage (fixed):** in the first `--gui` measurement `SceneHierarchyPanel.cpp` was 4.5 % and the
  asset inspector 5.6 % - the smoke test selected every entity and added every component, but the Inspector window's
  tab stayed behind windows opened in earlier steps and was not drawn at all (a hidden tab draws nothing). Now the
  Inspector is brought to the front in those steps (`EditorSmokeTest.cpp`): Editor/Panels 21.2 % -> 41.6 %, the editor
  total 32.1 % -> 43.6 %, and that every component inspector draws without errors is really checked.
- **The effect of the instrumentation:** in the coverage build the `JobSystemSpeedupAndStats` test cannot measure the
  speed-up (the gcov counts are atomic and all threads update the same counts: 4 threads came out slower than 1). In
  that build (`ZAMIN_COVERAGE_BUILD`) only the speed-up check is skipped; the results and statistics are checked.
  The tests are ~3.5 times slower with coverage (136 s, 39 s in a normal Debug build).

### Differences from other tools

`--html` (gcovr) shows every file line by line; its total percentage comes out a little lower (for example 62.8 % and
65.6 % from the same data), because gcovr counts template and lambda instances as separate lines. The modules table
counts each source line once (like lcov) - use the same tool when comparing.
