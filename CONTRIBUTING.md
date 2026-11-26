# Contributing to Zamin Engine

If you want to change Zamin — a bug fix, a new feature, a sample or documentation — this guide explains
building, code style, tests, the language of texts, documentation and the commit rules. The internals:
[docs/Architecture.md](docs/Architecture.md); all the documents: [docs/README.md](docs/README.md).

Contents:

1. [Building and running](#1-building-and-running)
2. [Code style](#2-code-style)
3. [Texts: English, `Tr()` and localization](#3-texts-english-tr-and-localization)
4. [Tests](#4-tests)
5. [Sanitizers, coverage, benchmarks](#5-sanitizers-coverage-benchmarks)
6. [A new component or script API](#6-a-new-component-or-script-api)
7. [Documentation and pictures](#7-documentation-and-pictures)
8. [Commit rules](#8-commit-rules)
9. [The task list and the CHANGELOG](#9-the-task-list-and-the-changelog)
10. [Before a pull request](#10-before-a-pull-request)

---

## 1. Building and running

Requirements and other distributions: [BuildAndExport.md](docs/BuildAndExport.md#1-talablar). A Debug build is
convenient for development (asserts are on):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # or: cmake --preset debug
cmake --build build 2>&1 | grep -E "error|warning"       # must print nothing
./build/bin/ZaminTests                                   # the last line: "N tests, 0 failures"
```

- Source files are collected with a glob: after adding a **new** `.cpp` / `.h`, run `cmake -S . -B build`
  again.
- The sample games (`Projects/Platformer2D`, `Showcase3D`, `Adventure3D`) are part of the main build: when the engine API
  changes their scripts are compiled too — they must not break either.
- Presets (`CMakePresets.json`): `debug`, `release`, `clang`, `asan`, `ubsan`, `lsan`, `tsan`, `coverage`.
  The CMake options: [BuildAndExport.md, section 3](docs/BuildAndExport.md#3-cmake-variantlari).
- Checking the editor and the player without a display (headless, `xvfb-run`; the automation variables of the editor
  and the player — [Editor.md, section 15](docs/Editor.md#15-avtomatlashtirish-skrinshotlar-ci) and
  [Runtime.md, section 14](docs/Runtime.md#14-avtomatlashtirish-uchun-muhit-ozgaruvchilari)):

```bash
ZAMIN_MAX_FRAMES=60 ZAMIN_SCREENSHOT=/tmp/editor.png xvfb-run -a -s "-screen 0 1600x900x24" \
    ./build/bin/ZaminEditor Projects/Showcase3D 2>&1 | grep -iE "error|assert"
ZAMIN_MAX_FRAMES=60 ZAMIN_SCREENSHOT=/tmp/game.png xvfb-run -a -s "-screen 0 1600x900x24" \
    ./build/bin/ZaminPlayer Projects/Adventure3D 2>&1 | grep -iE "error|assert"
```

## 2. Code style

The style is in the `.clang-format` file (clang-format 17+): it describes the **existing** style and is not meant for
reformatting the code base.

- C++20, everything inside `namespace Zamin`; 4 spaces, no tabs; braces on a new line (Allman) —
  except `namespace Zamin {`, whose body is indented.
- `Type* name`, `Type& name`; `template<typename T>`; `{ 1.0f, 2.0f }` (a space inside the braces).
- No column limit: lines broken by hand are kept (usually ~120–160 columns); the include order is not changed.
- Names: classes, functions, component fields — `PascalCase` (`TransformComponent::Translation`,
  `Scene::OnUpdateRuntime`); member variables — `m_` (`m_ActiveScene`), statics — `s_`; macros — `ZM_`.
- `Ref<T>` = `std::shared_ptr<T>`, `Scope<T>` = `std::unique_ptr<T>` (`Core/Base.h`).
- Logging: `ZM_CORE_INFO` / `ZM_CORE_WARN` / `ZM_CORE_ERROR` in the engine, `ZM_INFO` /
  `ZM_WARN` / `ZM_ERROR` in the editor, the player and games — with `std::format` syntax (`ZM_WARN("File not found: {}", path)`). Checks:
  `ZM_ASSERT` / `ZM_CORE_ASSERT` (only in builds with asserts on) and `ZM_VERIFY` (always evaluated) — with a message
  ([Core.md, section 12](docs/Core.md#12-assert-va-verify)).
- Code comments are in **English**. Public headers carry Doxygen comments (`///`, `///<`): write a short, precise
  comment for a new public class or function — `cmake --build build --target docs` must stay free of warnings
  (`build/docs/doxygen-warnings.log`).
- The build has **no warnings**: our targets use `-Wall -Wextra -Wpedantic` with GCC and Clang
  (`zamin_target_settings`, `cmake/ZaminGame.cmake`).

Check only the lines you changed yourself:

```bash
Tests/format_check.sh                 # uncommitted changes (against HEAD)
Tests/format_check.sh --base main     # since the branch left main
Tests/format_check.sh --fix --base main
```

New files must be added to git (`git add` or `git add -N`). `vendor/` is never formatted.
There are house habits clang-format cannot express — several short statements on one line (`a.X = 1; a.Y = 2;`), a
short one-line `enum` — a suggestion on such a line may be ignored; that is why the `format` job in CI
only warns. The difference for the whole code base: `Tests/format_check.sh --stats` ([Quality.md](docs/Quality.md#formatlash)).

## 3. Texts: English, `Tr()` and localization

- **Every** text a user or a developer sees — the editor interface, log and error messages, the player's windows, the
  output of tests and tools, documentation — is in English. Code comments and identifiers are in English too.
- **Editor texts** go through `Tr()` (`Zamin/Editor/EditorLanguage.h`): for historical reasons the key is the Uzbek
  text, and its English text — what the editor shows by default — is in the `s_English` table of
  `Zamin/src/Zamin/Editor/EditorLanguage.cpp`. Uzbek remains an optional translation that can be selected in the
  preferences. A leading icon (`ICON_FA_...`), a trailing `...` / `:` and an ImGui id (`##id`, `###id`) are not part of
  the key:

  ```cpp
  if (ImGui::MenuItem(Tr(ICON_FA_GEAR "  Loyiha sozlamalari...")))   // in the table: { "Loyiha sozlamalari", "Project settings" }
      m_ShowProjectSettings = true;
  ImGui::Begin(Tr(ICON_FA_STOPWATCH "  Profiler###Profiler"));       // ### - the window id stays the same when the language changes
  ```

  Helpers such as `UI::PropertyLabel` and `UI::PropertyHeader` (`Editor/src/UI.h`) call `Tr()` themselves. When the same
  Uzbek word has two meanings — `EditorText::Choose("Bekor qilish", "Undo")`. A key with no pair in the table
  is shown as it is, in Uzbek — so **every new key needs its English pair in the table**. The interface language can be
  forced with `ZAMIN_EDITOR_LANGUAGE=en` / `uz`. The `EditorLanguageStrings` test checks that the table has no duplicate
  or empty entries.
- **Log messages** are not translated — they are written directly in English: `ZM_CORE_ERROR("The scene could not be loaded: {}", path)`.
  If you change a message quoted in [FAQ.md](docs/FAQ.md), update the FAQ too (`DocsCheck` checks it).
- **Game texts** (what the player sees) are the game's business, not the engine's: `Assets/Localization/<language>.json` and
  the localization API in scripts ([ProjectSettings.md](docs/ProjectSettings.md), [Scripting.md](docs/Scripting.md)).

## 4. Tests

| What changed | What to run |
|---|---|
| Any change | `cmake --build build` (no warnings) and `./build/bin/ZaminTests` (`N tests, 0 failures`) |
| Engine logic | A new or extended unit test in `Tests/ZaminTests.cpp` |
| Documentation, components, the script API, player options | `ctest --test-dir build -L docs` |
| Rendering | `Tests/render_tests.sh` (`--update` when the look changed on purpose) |
| Editor | The editor smoke test: `Tests/editor_smoke.sh` (or `ctest --test-dir build -L gui`) |
| The script module, export | `Tests/hot_reload_test.sh`, `Tests/export_test.sh` |

Unit tests are headless (no GPU and no window) — **do not create textures, meshes, fonts or shaders**. A scene with no
primary camera is not drawn, so its runtime update can be called freely in a test. How a test is written:

```cpp
TEST(MyFeatureRoundTrip)
{
    Ref<Scene> scene = CreateRef<Scene>();
    Entity e = scene->CreateEntity("Crate");
    e.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    CHECK(e.GetComponent<TransformComponent>().Translation.y == 2.0f);
    CHECK_NEAR(e.GetComponent<TransformComponent>().Translation.z, 3.0f, 1e-5f);
}
```

- Put the test under the section comment (`// ---------- ...`) that fits its subject, then update the list:
  `Tests/test_catalog.py --update docs/Testing.md` (`DocsCheck` finds a stale list).
- For every bug you fix, write a test that keeps it from coming back ([Testing.md](docs/Testing.md#tests-that-guard-found-bugs)).
- Running one test: `./build/bin/ZaminTests MyFeature*` (a glob, `-` to exclude, `--list`,
  `--timing`) — [Testing.md, section 1](docs/Testing.md#1-unit-tests---zamintests).

## 5. Sanitizers, coverage, benchmarks

- If you change code that deals with memory, threads or physics / audio — the sanitizers:
  `Tests/sanitize.sh address --smoke --editor-smoke` (ASan + LSan) and `Tests/sanitize.sh undefined --compiler clang`.
  They build in separate `build-asan` / `build-ubsan` folders. A bug in our own code is never written into
  `Tests/sanitizers/*.supp` — it is fixed; only the noise of third-party code goes there (with a comment).
- Code coverage: `Tests/coverage.sh [--gui] [--html]` ([Coverage.md](docs/Coverage.md)).
- For a change that affects speed: `Tests/bench.sh --compare` (against the Release baseline) and, when needed,
  `Tests/render_bench.sh` ([Benchmarks.md](docs/Benchmarks.md)).

## 6. A new component or script API

A new component — the list in [Architecture.md, section 17](docs/Architecture.md#17-yangi-komponent-qoshish):
`Components.h` (the struct + `AllComponents`), `ComponentRegistry`, `SceneSerializer.cpp` (writing and reading),
`SceneHierarchyPanel.cpp` (the `AddSection<T>` inspector section and `DisplayAddComponentEntry<T>`), both variants of
`ResetRuntimeData` in `Scene.cpp` if it has runtime fields, a unit test, then the documentation:
a `### NameComponent` section with a hand-written description in [Components.md](docs/Components.md), then
`python3 Tests/docs_components.py --update docs/Components.md` (the field table is generated from the code).

A new public function of `ScriptableEntity` — mention it in [Scripting.md](docs/Scripting.md) (the API reference
table). A new command line option of the player — in [Runtime.md](docs/Runtime.md); a new `ZAMIN_*` CMake option —
in [BuildAndExport.md](docs/BuildAndExport.md). `DocsCheck` checks these.

## 7. Documentation and pictures

- The documents are in `docs/`, in **English**, with PascalCase file names (`Scripting.md`, `BuildAndExport.md`). Add a new
  document to the [docs/README.md](docs/README.md) index. Do not repeat an existing document — link to it.
- Every class, function, field, macro, menu path, environment variable and file name must **really exist** in the
  code — check with `grep` before writing. Code examples must work: whole script classes in the documents
  (`class X : public ScriptableEntity`) are compiled by the `DocsSnippets` test.
- The checks: `ctest --test-dir build -L docs` — `DocsCheck` (`Tests/docs_check.py`: links and `#anchor`s,
  components, the script API, player options, FAQ messages, CMake options, the test catalog, the CHANGELOG,
  the index, pictures) and `DocsSnippets` (`Tests/docs_snippets.py`).
- **Pictures** are in `docs/images/`: real editor / player screenshots (headless, with the commands above and the
  `ZAMIN_EDITOR_*` variables), no wider than **1280 px**, each up to **400 KB** (`DocsCheck`
  checks this), each used in some document. The recommended conversion:

  ```bash
  convert shot.png -resize '1280x>' -strip -interlace JPEG -quality 85 docs/images/topic-what.jpg
  ```

  Write a caption in italics under a picture: which project / scene, what is shown.

## 8. Commit rules

- The commit title is a short summary in **English**. Through M25 titles started with the milestone number
  ([docs/TASKS.md](docs/TASKS.md)) and were in Uzbek: `M22: 436-440 bajarildi - .clang-format, CI sanitizers job ...`;
  unfinished work was `M22 (jarayonda): ...`, a fix of earlier work `M22 tuzatishlar: ...`. `Tests/changelog.py` splits the
  commits into milestones by this prefix (a commit without a prefix joins the previous one).
- Every commit builds and passes the tests (for bisecting); build output (`build/`,
  `Projects/*/Binaries/`, `Projects/*/.zamin/`) is not committed — `.gitignore` keeps it out.
- One commit — one logical change (a big milestone is split into several commits). History is not rewritten
  (no `push --force`).
- Code goes in one commit together with its test and its documentation.

## 9. The task list and the CHANGELOG

- After a task is done **and checked**, change `- [ ] NNN.` to `- [x] NNN.` in [docs/TASKS.md](docs/TASKS.md).
  Do not mark what is only partly done.
- [CHANGELOG.md](CHANGELOG.md) — by milestone, the newest at the top. When a milestone's first task is
  done, add its `## Mnn Name` section and write the main changes as a short list. The first line under
  the section comes from the git history:

  ```bash
  python3 Tests/changelog.py               # the right line for every milestone (date, tasks, commits)
  python3 Tests/changelog.py --commits M23 # a milestone's commits - for writing the summary
  python3 Tests/changelog.py --check       # does CHANGELOG.md match the history (DocsCheck does the same)
  ```

  In the newest milestone's line only the first commit is checked (it grows with every commit); when the next
  milestone starts, update the previous one's line in full (the number and the range of commits).

## 10. Before a pull request

- [ ] `cmake --build build` — no errors and no warnings (GCC; Clang too if you can: `cmake --preset clang`).
- [ ] `./build/bin/ZaminTests` — `0 failures`; `ctest --test-dir build -LE gui` — everything passed.
- [ ] If the editor, rendering or the player changed — the headless smoke (section 1) or `ctest --test-dir build -L gui`.
- [ ] `Tests/format_check.sh --base main` — the new lines follow the style.
- [ ] New user texts are in English; editor texts go through `Tr()` (with their English pair in the table).
- [ ] The documentation is updated (`ctest --test-dir build -L docs`), a picture if needed; TASKS.md and CHANGELOG.md.

CI (`.github/workflows/linux.yml`) runs the GCC and Clang builds, the unit tests, the rendering,
editor smoke, hot reload and export tests, and the sanitizer, format and coverage jobs on every push and pull request
([Testing.md](docs/Testing.md#ci-githubworkflowslinuxyml)).
