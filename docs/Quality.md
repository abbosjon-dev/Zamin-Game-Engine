# Quality report

Two measurements: first the project's **final regression** (M25, task 500, 2026-11-09), then the detailed report from
the end of the M22 "Quality and tests" milestone (sanitizers, coverage, formatting statistics). Every number was
actually measured on this computer (4 vCPU, 15 GB RAM, Ubuntu 24.04, GCC 13.3, Clang 18.1, Mesa llvmpipe - no GPU).
The tests and how to run them: [Testing.md](Testing.md); performance: [Performance.md](Performance.md).

## Final regression (M25, 2026-11-09)

After all 500 tasks (`c2b82dd` + a hot reload fix). Every build from scratch or fully configured; the ctest suites in
the Debug build, with other builds running at the same time (so the times are longer than usual).

| Check | Result |
|---|---|
| GCC 13 Debug (`build/`) | build: **0** warnings; `ZaminTests` **651 tests, 0 failures** (53 s) |
| GCC 13 Release (a separate build folder, complete: engine, editor, player, tests, benchmarks, 9 game modules) | build: **0** warnings; `ZaminTests` **651 / 0** (12 s) |
| Clang 18 Debug (`build-clang`, complete: 437 steps) | build: **0** warnings; `ZaminTests` **651 / 0** (38 s) |
| ASan + UBSan (GCC Debug, `Tests/sanitize.sh address,undefined`) | `ZaminTests` **651 / 0, 0 sanitizer reports** (198 s) |
| Rendering references (`Tests/render_tests.sh`) | **12 / 12** - in the GCC Debug, GCC Release and Clang builds |
| Documentation (`DocsCheck`, `DocsSnippets`) | 0 problems (29 documents, 35 images); documentation code: 25 script classes, 12 files compiled |
| Benchmark smoke (`BenchEcsSmoke`, `BenchSerializeSmoke`) | passed |
| Editor smoke test (`EditorSmoke`) | Showcase3D **221 / 221**, Platformer2D **263 / 263** steps, 0 errors, 0 OpenGL flags, 0 ImGui checks |
| Script hot reload (`ScriptHotReload`) | passed (21 s) - failed on the first run, fixed (below) |
| Export test (`ExportTest`) | passed (137 s) |
| Sample games (`ctest -L samples`: `SampleAssets` + 9 games) | **10 / 10** passed |
| Exported samples (`SampleExports`) | **9 / 9** samples exported and played from the exported folder (6 bot demos, 3 smoke runs; 891 s) |
| Physics determinism (Box2D, Jolt) | inside `ZaminTests`, passed in every build |
| Formatting (`Tests/format_check.sh --base 14e8435`) | 2 hunks fixed in the changed lines, clean now |
| Code size | 528 C++ files (without vendor): engine 385 files / 122 600 lines, editor 57 900, tests 53 800, games 14 900 |

**A bug found by the regression.** `ScriptHotReload` failed under heavy load (two builds at once): the editor read the
module file before the linker had finished writing it - `Script module failed to load: ... file too short`; it loaded on
the next request, but the error in the log failed the test. A file size and time that do not change over two requests
(0.4 s) turned out not to be enough: the linker can pause that long. Now the editor also checks that the section table
in the ELF header (the linker writes it last) lies inside the file (`IsElfFileComplete`, `EditorLayer.cpp`); on a rerun
the test passed.

## M22 report: summary

| Metric | Result |
|---|---|
| Unit tests (`ZaminTests`) | **627 tests, 0 failures** - GCC Debug 39 s, ASan 107 s, Clang UBSan 48 s |
| Rendering reference tests | **12 / 12** passed (llvmpipe, 480x270, threshold 0.5 % of pixels) |
| Editor smoke test | Showcase3D **221 / 221** steps, Platformer2D **263 / 263** steps, 0 errors, 0 OpenGL errors, 0 ImGui checks |
| Script hot reload test | passed (19 s): field values are kept (`Speed=7.5`, `Count=3`), a new field `Extra=42`, the new code runs, no errors in the log |
| Export test and game builds | **3 / 3** games exported, checked and packaged (11-14 MB `.tar.gz`) |
| Physics determinism | Box2D and Jolt: 4 runs (twice, after save / load, built in reverse order) identical to the bit |
| AddressSanitizer + LeakSanitizer | **0 reports**: unit tests, 3 games, the editor, the editor smoke test (484 steps) |
| UndefinedBehaviorSanitizer | **0 reports** in our code: GCC (the previous measurement) and Clang (this measurement) |
| Code coverage (gcov) | engine **82.6 %** of lines (unit tests) / **89.3 %** (with `--gui`); editor 43.6 %; everything 70.0 % |
| Warnings (`-Wall -Wextra -Wpedantic`) | **0** - in the GCC Debug / Release, Clang Release and Clang Debug (UBSan) builds |
| Formatting (`.clang-format`) | 3.11 % of the code base's lines would change; 209 of 491 files would not change at all |
| Code size | 491 C++ files, ~233 500 lines (without vendor): engine 117 700, editor 57 300, tests 51 900 |

## Tests

| Layer | Count | Time | Note |
|---|---|---|---|
| Unit tests | 627 tests, 92 sections | 39 s (Debug) | without a GPU; the slowest: `PhysicsDeterminism2D`, `PhysicsDeterminism3D`, `SerializationFuzz` |
| Rendering references | 12 cases | 2 s (xvfb, llvmpipe) | `Tests/Reference/*.png` |
| Editor smoke test | 2 projects, 484 steps, 1 434 frames | 163 s (106 + 57 s) | every window, page, menu, popup, inspector |
| Hot reload test | 1 project, 2 versions | 19 s (editor compilation 7.9 s) | `Tests/Fixtures/HotReload` |
| Export test | 3 games x 5 stages | 84-109 s per game | the package is unpacked and run again |
| ctest | 6 tests (`gui` label - 3, `bench` - 2) | | `ctest -LE gui` in CI |
| Benchmark smoke tests | 2 | seconds | `ctest -L bench` |

How the unit test time is spread (Debug): 244 tests faster than 1 ms, 217 - 1-10 ms, 119 - 10-100 ms, 38 - 0.1-1 s,
9 - slower than 1 s (they are 60 % of the total time).

## Sanitizers

| Build | What was run | Result | Time |
|---|---|---|---|
| ASan + LSan (GCC Debug, `build-asan`) | `ZaminTests` | 627 tests, 0 failures, 0 reports | 107 s |
| | `ZaminPlayer` Platformer2D / Showcase3D / Adventure3D (60 frames, xvfb) | 0 reports | 7 / 17 / 16 s |
| | `ZaminEditor` Showcase3D (60 frames) | 0 reports | 16 s |
| | the editor smoke test (Showcase3D 221 steps, Platformer2D 263 steps) | 0 errors, 0 reports | 205 s |
| UBSan (Clang Debug, `build-ubsan`) | `ZaminTests` | 627 tests, 0 failures, 0 reports | 48 s |
| UBSan (GCC), LSan separately | `ZaminTests` (the previous measurement, 421-423) | 0 reports | |
| TSan | only its configuration was checked | not run | |

Suppression files (`Tests/sanitizers/`), each with a comment - all of it third-party code:

| File | Entries | What |
|---|---|---|
| `lsan.supp` | 3 | miniaudio's PulseAudio backend (the context is not freed when there is no server), its JACK probe, Mesa llvmpipe's cache after `glGenerateMipmap` |
| `ubsan.supp` | 1 | miniaudio's MurmurHash3 adds a negative index to a pointer (`pointer-overflow`) - found by Clang UBSan |
| `asan.supp`, `tsan.supp` | 0 | |

Bugs the sanitizers found during the milestone, all fixed:

1. **ASan, use-after-free** - deleting a mixer group (with several child groups / sounds) and `AudioEngine::Shutdown`:
   miniaudio's `ma_node_uninit` detached only the first input node. Fixed with `DetachAllInputs`, test
   `AudioMixerGroupTeardown`.
2. **UBSan** - an invalid value in a test enum (outside the enum's range); the `ZM_FIELD_ENUM` rule in [Scripting.md](Scripting.md#4-inspektor-maydonlari-zm_field).
3. **Clang UBSan, pointer-overflow** (this measurement) - the miniaudio implementation was compiled inside the engine's
   `AudioEngine.cpp`, so a report from third-party code was as fatal as one from our own code (the tests stopped at
   test 150). The implementation moved to its own translation unit (`Zamin/src/Zamin/Audio/MiniaudioImpl.cpp`, the
   configuration - `Miniaudio.h`): in a sanitizer build it is recoverable like the vendor libraries, with the report
   commented in `ubsan.supp`; in the rest of the engine's code every report is still fatal.

## Code coverage

`Tests/coverage.sh` (GCC, `--coverage -O0`), 2026-11-08 (details: [Coverage.md](Coverage.md)):

| Part | Unit tests | With `--gui` (player, editor smoke) |
|---|---|---|
| Engine (Zamin), 59 655 lines | **82.6 %** of lines / 86.3 % of functions | **89.3 %** / 92.4 % |
| Editor, 38 277 lines | - | 43.6 % / 57.5 % |
| Player | - | 33.5 % / 50.0 % |
| Sample games | - | 27.3 % / 29.3 % |
| Everything, 101 402 lines | 82.6 % | 70.0 % / 82.6 % |

The least covered engine modules (unit tests): `Zamin/Platform` 8.2 % (window, OpenGL - needs a GPU; 59.0 % with
`--gui`), `Zamin/Renderer` 56.9 % (85.3 %), `Zamin/Scripting` 67.8 % (module loading - covered by the hot reload test).

## Warnings

Our targets (engine, editor, player, tests, benchmarks, sample game modules) build with `-Wall -Wextra -Wpedantic`,
vendor headers through `-isystem` (our `Zamin/src` is never a system folder - otherwise warnings in the engine headers
would be hidden in the engine's own files; `cmake/Dependencies.cmake` checks this); only
`-Wno-missing-field-initializers` (listing the first fields of the settings structs is deliberate, ~70 places).

| Build | Steps | Warnings |
|---|---|---|
| GCC 13 Debug (`build/`) | complete | 0 |
| GCC 13 Debug (`build/`), `Zamin/src` now `-I` in the engine files too (before, `-isystem` through ImGui) | complete rebuild (498 files) | 0 |
| Clang 18 Release, in the same state | engine (`libZamin`, 193 files) | 0 |
| GCC 13 Debug + ASan (`build-asan`, this measurement) | 517 (engine, editor, player, tests, 3 modules, vendor) | 0 |
| Clang 18 Debug + UBSan (`build-ubsan`, this measurement) | 432 (engine, tests, vendor) | 0 |
| GCC Release, Clang Release (the task 424 measurement) | complete | 0 |

## Formatting

`.clang-format` describes the existing style (4 spaces, Allman braces, `namespace Zamin {`, no column limit).
`Tests/format_check.sh --stats` (clang-format 18.1.3): of 491 files and 233 505 lines **7 264 lines (3.11 %)** would
change, **209 files** would not change at all. The kinds of difference (`Tests/format_stats.py` sorts them roughly):

| Kind | Lines | Reason |
|---|---|---|
| one-line `{ ...; ...; }` blocks | 1 547 | clang-format never leaves several statements on one line |
| short one-line enums | 1 171 | with braces on a new line clang-format 18 expands every enum |
| several statements on one line (`a.X = 1; a.Y = 2;`) | 843 | (as above) |
| hand-wrapped `{...}` lists | 829 | without a column limit clang-format does not keep the breaks inside a list |
| ?: chains | 786 | it aligns every condition of the chain on its own line |
| several elements on one line (enumerators, lists) | 679 | it puts every element on its own line |
| hand-aligned case lines | 280 | the alignment style differs from file to file |
| other (wrapping, indentation) | 1 129 | partly real inconsistencies (wrong indentation) |

So the code base is not reformatted, and the CI check looks only at changed lines and only warns. While tuning,
ColumnLimit 120 / 140 / 160 were tried (21.6 / 17.3 / 16.8 % difference), `0` is the closest;
`LambdaBodyIndentation: OuterScope` was chosen - with the default value clang-format 18 pulled the closing bracket onto
the last statement's line in calls that take several multi-line lambdas (`...; }, [this]() {` - 93 lines, 6 files).

## Benchmarks (Release baseline results)

Details: [Benchmarks.md](Benchmarks.md). Rendering - llvmpipe (a software rasterizer), so the absolute ms are much
larger than on a real GPU; the numbers are for comparing changes.

| Benchmark | Result |
|---|---|
| 10 000 sprites | 24.5 ms / frame (with the GPU), 3 draw calls |
| 1 000 meshes, with / without instancing | 136.5 / 141.7 ms, 46 / 2 027 draw calls |
| ECS: 10 000 `CreateEntity` | 2.29 ms (229 ns each) |
| ECS: `Scene::Copy` of 10 000 objects (editor Play) | 6.93 ms |
| ECS: 1 000 script `OnUpdate` | 0.308 ms / frame |
| Serialization: a 2D scene with 10 000 objects, JSON save / load | 119 / 176 ms (8.8 MB) |
| Serialization: loading with the binary cache | 99 ms (3.2 MB) |
| Exported game (Debug engine, llvmpipe, 120 frames) | Platformer2D, Showcase3D, Adventure3D - `benchmark.json` in the artifact |

A problem the benchmarks found, now fixed: the scripts' `Destroy` queue was quadratic (10 000 of them - 525 ms, now
87 ms in Debug; `DeferredDestroyOnceEachAndLinear`).

## Other bugs found during the milestone

| Tool | Bug | Fix |
|---|---|---|
| Physics determinism test | bodies / joints were created in component pool order: editor Play and the exported game simulated differently | create them in scene order, `Scene::Copy` in hierarchy order |
| Clang Release build | the particles' random numbers came in a different order in GCC / Clang; an FMA contraction difference; a memory test was optimized away | expressions split, `-ffp-contract=off`, a volatile write |
| Clang build | the tests did not compile at all (`std::pair` with an incomplete type) | a `Member` struct |
| Hot reload test | a module the linker was still writing got loaded; the log to a pipe was fully buffered; a 423 MB copy in `/tmp/zamin-modules` | wait for stability, line buffering, `RemoveStaleModuleCopies` |
| Code coverage | the editor smoke test never drew the Inspector (its tab was hidden) | the tab is brought to the front: Editor/Panels coverage 21.2 → 41.6 % |
| Editor smoke test | (no bugs found - a deliberately planted log error, GL error and ImGui `End()` error were caught) | |

## CI

`.github/workflows/linux.yml` - 4 jobs: `build` (GCC and Clang, Release: unit tests, rendering, editor smoke, hot
reload, export + game builds, screenshots), `sanitizers` (`asan`, `ubsan-clang`), `format`, `coverage`. Every step
calls a script that was also run on the local computer. Artifacts: unit test results (JSON, JUnit), rendering images
and diffs, editor smoke logs and images, export logs and screenshots, **game builds** (`player-builds-<cc>`: 3
`.tar.gz` + `.sha256`, 14 days), sanitizer logs, the coverage report.

Note: the workflow was not run on GitHub Actions during this work - the YAML was checked with Python and every step's
command was run on the local computer.

## Known gaps and next steps

- **ThreadSanitizer** has not been run yet (the job system, the audio thread, the asset loading threads).
- **Editor coverage** is 43.6 %: the click / drag paths of many panels are tested only by hand; the editor smoke test
  opens the windows but does not interact with them.
- Rendering is tested only on Mesa llvmpipe (no real GPU drivers, no other OpenGL implementations); there are no
  Windows / macOS builds.
- `clang-tidy` / static analysis has not been applied yet.
- `Zamin/Events` coverage is low (18.8 %, a small module) and `Zamin/Scripting` is 67.8 %.
- The formatting check only warns: clang-format cannot express some habits of the house style.

## Measuring again

```bash
./build/bin/ZaminTests --timing --slowest 10
Tests/render_tests.sh && Tests/editor_smoke.sh && Tests/hot_reload_test.sh && Tests/package_games.sh
Tests/sanitize.sh address --smoke --editor-smoke --clean && Tests/sanitize.sh undefined --compiler clang --clean
Tests/coverage.sh --gui --clean
Tests/format_check.sh --stats --top 20
Tests/bench.sh --compare; Tests/render_bench.sh
```


## Seven items from the initial audit

1. A scene save error does not clear the dirty / history / autosave state; an export does not go on with an unsaved scene.
2. The scene and project configuration are written atomically, flush / close errors are checked; on a replace error the old file is kept.
3. The editor's script build starts through `RunProcess(argv)`, without a shell. `$()` or a backtick in a folder name is not executed.
4. Incomplete or repeated shader stages are handled: a log and an empty shader, not `std::out_of_range`.
5. Undo has a default limit of 128 MiB of stored data. Scene copies, prefab file edits, names and selections are
   counted; allocator overhead and temporary serializer memory do not count toward this limit.
   When the current state alone is larger than the limit, it stays as the only state. A stored old snapshot is remembered with a weak reference.
6. The editor services moved into a separate library; the large inspector / build modules and the editor unit tests moved into separate files.
   The monolithic `EditorLayer.cpp` and the remaining main test file can be split further in later changes; this refactor does not rewrite the whole code base.
7. The default CPU profile is x86-64/SSE2. AVX2/FMA only with an explicit opt-in; [requirements](BuildAndExport.md#the-cpu-profile-and-a-runtime-without-the-editor-services).

Regressions: `SafetyTests.cpp`, `EditorServicesTests.cpp`, `TerrainTests.cpp`. `ZAMIN_EDITOR_TERRAIN=test` also checks
inside the real editor that dirty, history and autosave are kept after a save error.


Check results (2026-11-09): 645/645 unit tests, 12/12 render cases, editor smoke 265/265
(OpenGL/ImGui errors 0), docs check 0 problems. `ZAMIN_EDITOR_TERRAIN=test` confirmed that dirty/history/autosave are
kept on a save error. With the `ZaminStandalone` library, without ImGui, the real Player opened a map of 271 objects.
Hot reload passed with two script versions in a folder with a space, an apostrophe and `$()`; no shell marker file was created.
Under QEMU x86-64/SSE2 (SSE3/SSE4/AVX/AVX2/FMA/F16C/BMI/ABM/POPCNT off) the Jolt terrain collision
regression passed. The logs and JSON/JUnit reports are in `build-review/Audit/`.


### GPU terrain PBR — 2026-11-10

696/696 unit tests, 12/12 reference renders and the OpenGL 4.1 pixel-oracle terrain test passed. The caller's framebuffer is saved before the shadow atlas is resized; float paint normalization no longer calls a control upload on a repeated validation. The editor loaded four albedo/normal/ORM layers, and a map of 271 objects ran in the runtime without ImGui. Export reachability picked up all 12 source maps; the Player passed the screenshot and relocated-folder launch checks. The native SDK sample modules were rebuilt. The reports are in `build-review/TerrainPBR/`; the GPU driver matrix and large-world performance acceptance are still open.


### SDK ABI, GPU profiling and foliage authoring — 2026-11-10

The 707/707 unit and 12/12 reference render regression passed. A native fixture checked matching / mismatching / unmarked / stripped modules; the constructors of a mismatching module did not run, and a failed dlopen kept the previous class registry. A relocated SDK configured; when its header or toolchain was changed it was refused with a clear error. Editor hot reload kept a new field's default and the previous values. The native Platformer2D export ran with 26 classes, screenshot and relocation passed; an export with a mismatching module reported `Succeeded=false`.

The GL 4.1 GPU timestamp suite recorded 34 completed samples, 1 pending, 0 dropped; this is correctness evidence on software Mesa. Foliage API: region/offset, world normal (nonuniform parent), lock/hidden, invalid/runtime state, hierarchy/save/copy tests. In the real editor 89 roots were reprojected and Undo/Redo passed. Root/module/diagnostics/profiler screenshots and logs are in `build-review/SDKQuality/`. Separate GPU foliage culling/LOD and a physical GPU budget come in the next package.


## Upstream integration — 2026-11-10

The 13 commits on GitHub `main` up to `7f71a6c` were merged with the local terrain, foliage, visual graph, C++ editor and SDK/ABI work. The original history and both lines of work were kept. The terrain shader also uses the upstream `ZaminFrame` UBO; the upstream ELF linker-completion check was added to the relocated build/hot-reload code. The new UI keys were included in the translation coverage. A detach from a zero program on a shader link error was removed; the next terrain draw and a clean GL error state were checked in the regression. The visual graph GUI test allows time for pointer and button states to be observed under slow software rendering; the node, Undo/Redo and typed wire asserts are kept.

Linux Release / GCC 13.3 / Mesa llvmpipe check:

- 730/730 unit tests; all 8 non-GUI CTests (docs/snippets, bench smoke, sample assets, native ABI and the SDK manifest) passed.
- 12/12 reference renders; GL 4.3 foliage compute and the GL 4.1 CPU fallback, 0 pixel difference on 4096 instances / five cameras / the main images.
- GL 4.1 terrain PBR/cache/reload/shadow resize and shader link failure recovery; the GPU profiler and the runtime without UI.
- Showcase3D 224/224 and Platformer2D 266/266 editor steps: 0 GL/ImGui errors.
- The visual graph's real node/wire/Undo/Redo GUI test and the C++ editor's typing/save/completion/build/error navigation/recovery test.
- Real native hot reload v1→v2: the fields were kept and the scene file did not change.
- The FoliageWorkshop and Platformer2D exports: Player, screenshot, relocation; the native export loaded 26 script classes.

The evidence is local, in `build-review/IntegrationReview/`; these results do not replace frame-time acceptance on a physical GPU or on other OSes/drivers.


### CI compiler/sanitizer follow-up — 2026-11-10

Actions run `42877104904` after the `69140f2` push showed cases the local GCC checks
did not cover: `--no-undefined` in the Clang sanitizer DSO link and configuring the new native
module with the default GCC. The sanitizer DSOs use a policy that allows only the Clang runtime's
references; the normal build's no-undefined protection is kept.
`SanitizerSharedLink` confirmed launching a Clang ASan/UBSan/combined executable and detecting a real
overflow. The production export command picked the compiler with a real Clang SDK configuration,
while an explicit wrong GCC override was refused by the ABI gate.

The 128-byte leak stack in the ASan artifact goes through GLFW `extensionSupportedGLX` to
`glXQueryExtensionsString` (Mesa `25.2.8-0ubuntu0.24.04.4`). An LSan exception was added for that vendor
query only; Zamin/GlfwWindow and Mesa allocations in general are not excepted. A minimal GLFW probe on the
local `.2` Mesa package did not produce this leak; a 713-byte application allocation deliberately left behind
under that exception still showed up in the LSan report. For `63a6f67`, in [Actions 42882285887](https://github.com/abbosjon-dev/Zamin-Game-Engine/actions/runs/42882285887),
the Clang Release (with render/editor/hot reload/export), Clang UBSan, GCC ASan (with player/editor and all-panel
smoke), coverage and format jobs finished successfully. The full matrix's result is on that run's page.


## World-cell manifest v1 — 2026-11-10

The first part of P3: shared `.zworld` metadata for 2D/3D, double bounds, layer coordinates,
stable UUIDs and a deterministic dependency order. File/cell/dependency/nesting limits, duplicate JSON
keys, cycle and traversal/symlink checks; on a parse error the previous object is kept, save is atomic.
This stage does not include entity/GPU activation, runtime streaming, an editor panel or export integration.

Checks:

- The main Release build: the engine, every target and `ZaminStandalone` built successfully.
- The shared unit suite: **735/735**; non-GUI CTest: **9/9**, including docs/snippets, SDK/ABI and the sanitizer link regression.
- The five new manifest tests passed in a GCC and a Clang 18 standalone probe; Clang compiled with `-Wall -Wextra -Wpedantic -Werror`.
- The same five tests passed on the new manifest/parser code instrumented with ASan+UBSan; 0 sanitizer reports. The probe was linked against the UUID/FileSystem functions of the existing, uninstrumented engine library; this is not a sanitizer build of the whole engine.
- A 4096-cell dependency chain in reverse order, oversized/corrupt input, atomic file IO, symlink escape, worker parsing and keeping the previous state on an error were checked.
- A probe linked against the real runtime library without UI: roundtrip and failed-load preservation passed.

Evidence: local `build-review/WorldManifestReview/`; it also holds a screenshot of the existing FoliageWorkshop editor
with 4096 instances. The manifest metadata part does not change how the UI looks. Next: bounded async IO/decode,
versioned cancel/unload and main-thread activation/residency budgets.


## World-cell CPU staging v1 — 2026-11-10

`WorldCellLoader` takes the dependency closure from the manifest; with 1–4 private workers it performs bounded `.zscene` IO,
SAX preflight and the `SceneLoadJob` parse/migration. There are generational requests, stale-result discard,
input reservation, failure propagation and explicit retry. `Poll` announces ready results limited by
count; `TakeReady` hands the job of the matching generation to the caller. This is not runtime activation/unload,
texture decode/upload or real RAM/VRAM residency management; input reservation and DOM/scratch
memory are separate concepts. The limits and the shutdown/cancellation constraints are in [Terrain.md](Terrain.md).

Checks:

- The shared Release build: every target and `ZaminStandalone`; **744/744 unit**, **9/9 non-GUI CTest**.
- The nine new tests passed in GCC and Clang 18 probes with `-Wall -Wextra -Wpedantic -Werror`.
- Worker staging, generation/cancel/retry, dependency failure, backpressure with a single reservation,
  the camera demand switching 100 times and destruction with a pending worker were checked.
- Regressions for corrupt/large JSON, node/entity/nesting limits, duplicate keys, a future version,
  symlinks, FIFOs, directories and a swapped asset root passed.
- The real Platformer2D Level1 (old v1 → v5 migration, 108 entities) and FoliageWorkshop Forest
  (v5, 5 entities / 4096 packed instances) were parsed on a worker; the results match the normal scene parser,
  and no entity/GPU was activated during staging.
- The nine tests passed in an ASan+UBSan build of the new loader/probe code: 0 sanitizer reports.
  The existing engine library it links against is not instrumented; the whole-engine sanitizer check runs separately in the new CI.
- A separate probe linked against the runtime DSO without UI: worker staging → generation transfer → main-thread scene build passed.
- The TSan probe gave no result in this environment; even a minimal hello program printed
  `FATAL: ThreadSanitizer: unexpected memory mapping` at runtime start. No TSan coverage is claimed.

All 6 jobs of the [GitHub CI](https://github.com/abbosjon-dev/Zamin-Game-Engine/actions/runs/42886580325) for the previous `06d6055` push
finished successfully. This package's new remote CI result is assessed separately.
Evidence: local `build-review/WorldCellLoaderReview/`. Next: the main-thread activation/upload budget,
the physics/script lifecycle and safely unloading active cells.


## The large world streaming package — 2026-11-11 UTC

Delivered together: camera radius/hysteresis and XY/XZ/XYZ layer selection, a precomputed dependency
closure, slot/entity/declared CPU-GPU admission, bulk import and unload at the frame boundary, shared 2D/3D physics,
Project.StreamingWorld auto-start, an editor grid/policy/retry panel and two exportable demos.

Local GCC Release / Mesa software render check:

- **753/753 unit and 9/9 non-GUI CTest**: docs/snippets, benchmark smoke, ABI/SDK and the sanitizer link regression.
- Nine world tests: radius/hysteresis, offset/unload/return, dependency/layer/budget, UUID collision/retry,
  the script lifecycle once every entity exists, Stop/Single, export reachability.
- Real shared physics: a 2D and a 3D ball stood on a streamed floor and fell once the floor was unloaded.
- With a 4096-cell manifest and a 0.1 ms soft budget the last cell still activated; the planning cost did not cause starvation.
- A FIFO/malformed definition was refused; Save does not write the original scene file to a wrong extension.
- The same **9 tests also passed with the runtime DSO without UI**.
- The existing editor smoke: **224/224 Showcase3D and 266/266 Platformer2D** steps, 0 GL/ImGui errors.
- **12/12 reference renders** passed; this is a threshold-based regression, not a 0 pixel difference in every case.
- Clang 18 `-fsyntax-only -Werror`: the new runtime/import/manager/project/export and editor panel/build code passed.
- The real Player camera path: WorldWorkshop3D 660 frames, 21 activations / 15 unloads, peak admission 10;
  WorldWorkshop2D 660 frames, 14 activations / 11 unloads, peak admission 7. Both had failed=0,
  Active=0 in the empty area and Active>0 again on the way back. The editor world panel was also checked with a screenshot.
- Both demo exports: the Release native module, the standalone Player, a screenshot and relocation to another folder passed.
  An export with a missing world-cell scene returned `Succeeded=false` and a clear error.

The evidence is local, in `build-review/WorldPackage/`. Source/declared reservations are not real RSS/VRAM;
asset cache eviction, hard timing, a physical GPU budget, floating origin and HLOD are not marked as done.
A single entity build/resolve, activation or callback is not interrupted midway. The authoring limits and save
semantics are in [Terrain.md](Terrain.md). All 6 CI jobs of the previous `9d6ec43` push passed;
the new remote matrix for this larger package starts after the push, and the local Clang syntax check
does not replace a Clang/sanitizer build of the whole engine.


## The world asset streaming package — 2026-11-23

Three items of the P3 "world and asset streaming" track: a frame-time gate with strict budgets for texture/model
decode and GPU upload, RAM/VRAM residency limits and their regressions, and a repeatable camera path and reports with a
many-cell asset demo. The structure and limits —
[Terrain.md](Terrain.md#the-asset-budget-residency-limits-and-the-camera-path), the tests —
[Testing.md](Testing.md#world-asset-streaming-budget-limit-and-demo).

What was added:

- The `AsyncLoader` upload budget (bytes and calls, chunked uploads); a texture is uploaded row by row with a
  placeholder until it is ready; `TextureLibrary::Stage/LoadStaged/ReleasePending`, `Model::Stage` — decode/parse and
  the mip chain on a worker; `WorldAssetStager` — per-file / per-cell / total staging budgets; the `Staging` stage of
  `WorldStreaming` and the `AssetStreaming` policy (off by default: old worlds do not change).
- `EvictOnPressure`: at the RSS / requested-GPU limit the farthest cell is evicted, a cell is refused up front by its
  measured GPU size, heap memory is returned to the OS on a helper thread, `MaxAssetReleaseBytesPerFrame`.
- `WorldTour` (`.ztour`), the Player's `ZAMIN_WORLD_TOUR` / `ZAMIN_WORLD_REPORT` hooks, the asset/limit section of the
  World Streaming panel, `Projects/WorldGallery3D` and `Tools/make_world_gallery.py`.

Measurements (GCC Debug engine, Mesa llvmpipe `LP_NUM_THREADS=2`, 4 CPUs, xvfb; a 1080-frame tour, 240 warm-up frames).
The heavy world: 24 cells, every texture 2048² (16 MiB with mips), a model of 5184 triangles, a 640×360 window,
an upload budget of 2 MiB / 8 calls per frame:

| | with the budget | the old path (`AssetStreaming: false`) |
|---|---|---|
| image decode / model parse on the main thread | **0 / 0** | 118 / 8 |
| streaming work in one frame (world update + upload), max | **13.99 ms** | 88.66 ms |
| frames with more than 30 ms of streaming work | 0 | 108 |
| frame time p50 / p95 / p99 | 32.79 / 44.57 / 51.53 ms | 35.57 / 68.17 / 103.11 ms |
| frames 40 ms longer than the median (of them with streaming work) | 1 (0) | 39 (38) |
| bytes / calls sent in one frame, max | 2 097 152 / 2 | — |
| RSS peak / requested GPU peak | 839 / 406 MiB | 835 / 406 MiB |
| activations / unloads | 56 / 48 | 116 / 108 |

0 uploads over the budget, a staging peak of 256 MiB (= the limit), 15 cancelled cells, 345 uploads deferred to the next
frame. With the budget cells appear more slowly (a 16 MiB texture at 2 MiB/frame takes 8+ frames): that is why there
are fewer activations in the fast camera stage. The frame-time maximum (337 ms, 376 ms on the old path) is in both
cases the renderer compiling a shader variant for the first time — streaming work in that frame was 0.2 ms. With an
8 MiB cell budget 17 stagings were refused, 0 activations, 0 uploads.

Residency (the same heavy world):

- A two-lap tour without limits (2160 frames): 105 activations / 97 unloads, 30 cancellations, 0 failed, 0 decode/parse
  on the main thread; requested GPU in the empty area 342 → 342 MiB, lap peak 406 → 406 MiB, RSS peak
  819 → 819 MiB (no growth).
- A requested-GPU limit of 325.8 MiB (peak without a limit 406): peak **325.6 MiB** (did not reach the limit), 23 cells
  refused without loading, 2 evicted, up to 7 active cells, 0 failed.
- An RSS limit of 667 MiB (peak without a limit 819): 115 pressure frames, the heap returned to the OS 37 times (on a
  helper thread, the longest 8.4 ms), peak 709 MiB (42 MiB over the limit — work in flight when the limit was crossed),
  up to 9 active cells, 0 failed. The RSS limit is based on observation: it is not a hard ceiling.
- A broken world (256 px): a missing cell scene and corrupt cell JSON — exactly 2 failed; a corrupt and a missing texture,
  a corrupt model — 9 unreadable-file reports, the other cells active (up to 10), 0 decode/parse on the main thread.

Demo (`Projects/WorldGallery3D`, 512 px, 1280×720): two tours — the same camera path, at the rest points
(frames 239 / 419 / 659 / 919 / 1079) active cells 8 / 4 / 5 / 0 / 8, 79 activations / 71 unloads in both;
frame time p50 66.86 ms, p95 98.11, p99 111.84; streaming work at most 3.54 ms in all three tours; RSS 303 → 326 MiB, requested GPU
100 → 110 MiB. Export: 190 files, 52.4 MB; the same tour in the relocated folder — the same camera and rest points,
p50 69.91 ms, RSS peak 295 MiB, 0 unreadable files. The project is byte-identical to the generator output.

Tests: 1368/1368 unit (15 new, without GL: upload budget, stager, policy, limit, tour, the demo save/reload/export
plan), `--quick` 1356/1356; non-GUI CTest 17/17, docs 2/2; world/residency GUI CTest 11/11 — with `AssetResidencyGPU`
(real GL: row-by-row upload, a staged cell, return, cancellation, fast camera, release budget, Retry) and three
`WorldAssetStreaming_*` stages; 12/12 reference renders; EditorSmoke.

Limits: llvmpipe results are not an estimate of physical GPU speed. The byte budget bounds the work; creating /
destroying GL objects takes the driver's time (on llvmpipe a single upload chunk with a 16 MiB texture's storage takes up to 14 ms; in the GPU-limit tour
a single world update with a cache release took 13.8 ms, an upload 20.5 ms). Requested GPU bytes are not physical VRAM. The decode of terrain layer
textures, fonts, animation clips and embedded images does not count toward the budget. The remote
compiler/sanitizer matrix was not run for this package (not pushed).
Evidence: local `build/WorldAssetStreaming/Final/` (reports, logs, screenshots).

