add_executable(ZaminTests SliceUILayoutTests.cpp LoadoutModelTests.cpp LoadoutWorldTests.cpp ProjectileModelTests.cpp EffectModelTests.cpp CombatWorldTests.cpp CombatEffectTests.cpp CombatSaveTests.cpp CombatAuthoringTests.cpp CombatWorkshopTests.cpp LanternKeeperTests.cpp BeaconPassTests.cpp QuestModelTests.cpp QuestWorldTests.cpp GameplayUIModelTests.cpp GameplayUITests.cpp SaveModelTests.cpp SaveWorldTests.cpp TransferTests.cpp SliceEngineTests.cpp BrainModelTests.cpp GameplayAITests.cpp RulesModelTests.cpp GameplayRulesTests.cpp GameplayMathTests.cpp GameplayRuntimeTests.cpp AnimatorControllerTests.cpp AnimatorRuntimeTests.cpp BlueprintTests.cpp BlueprintNativeTests.cpp NavRecoveryTests.cpp NavCrowdTests.cpp NavQueryQueueTests.cpp NavAtlasTests.cpp ZaminTests.cpp WorldCheckpointTests.cpp ResidencyTerrainNavTests.cpp WorldStreamingTests.cpp WorldAssetStreamingTests.cpp WorldCellLoaderTests.cpp WorldManifestTests.cpp PackedFoliageTests.cpp ModuleABITests.cpp TerrainFoliageTests.cpp CodeDocumentTests.cpp LspTests.cpp VisualGraphTests.cpp TerrainTests.cpp TerrainLODTests.cpp TerrainStreamingTests.cpp AtlasCookTests.cpp TilemapVisibilityTests.cpp SafetyTests.cpp EditorServicesTests.cpp TestFramework.cpp DebugDrawShippingCheck.cpp)
target_link_libraries(ZaminTests PRIVATE Zamin ZaminEditorSupport)
# The sample projects are read by some tests (their assets must stay valid).
target_compile_definitions(ZaminTests PRIVATE ZAMIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_executable(ZaminSavePermissionProbe SavePermissionProbe.cpp)
target_link_libraries(ZaminSavePermissionProbe PRIVATE Zamin)
zamin_target_settings(ZaminSavePermissionProbe)
add_dependencies(ZaminTests ZaminSavePermissionProbe)
target_compile_definitions(ZaminTests PRIVATE ZAMIN_SAVE_PERMISSION_PROBE="$<TARGET_FILE:ZaminSavePermissionProbe>")
set_target_properties(ZaminTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
zamin_target_settings(ZaminTests)
# A coverage build: the tests that measure a speedup skip that check (gcov's counters are shared by the threads).
if(ZAMIN_COVERAGE)
    target_compile_definitions(ZaminTests PRIVATE ZAMIN_COVERAGE_BUILD=1)
endif()
# ctest also writes the results with every test's time (build/TestResults: JSON, JUnit XML for CI) and prints the
# time report (the slowest tests). ZaminTests --help: filters by name (--filter Prefab*, --list) and the other options.
# The unit tests (label "unit": the quick loop, ctest -L unit) and the whole slice playthroughs (SLOW_TEST, label
# "slices": ~85 % of a full run) are two entries, so ctest -j runs them side by side; both are in ctest -LE gui and CI.
add_test(NAME ZaminTests COMMAND ZaminTests --quick --timing --report ${CMAKE_BINARY_DIR}/TestResults/ZaminTests.json
    --junit ${CMAKE_BINARY_DIR}/TestResults/ZaminTests.xml)
set_tests_properties(ZaminTests PROPERTIES LABELS "unit")
add_test(NAME ZaminSliceTests COMMAND ZaminTests --slow --timing --report ${CMAKE_BINARY_DIR}/TestResults/ZaminSliceTests.json
    --junit ${CMAKE_BINARY_DIR}/TestResults/ZaminSliceTests.xml)
set_tests_properties(ZaminSliceTests PROPERTIES LABELS "slices;slow" TIMEOUT 1800)
# The code editor's clangd client against the real clangd (part of ZaminSliceTests too; it prints SKIP and passes when
# clangd is not installed): diagnostics while typing, member completion, go-to-definition, a cross-file rename, a crash.
add_test(NAME ClangdLsp COMMAND ZaminTests --slow --filter CodeIntelligenceRealClangd)
set_tests_properties(ClangdLsp PROPERTIES LABELS "clangd" TIMEOUT 300)

# The documentation stays true to the code (Tests/docs_check.py, label "docs"): every relative link and image of the
# Markdown files resolves, every component of AllComponents is in docs/Components.md (its generated tables up to
# date), every public ScriptableEntity function is in docs/Scripting.md, every player option of --help is documented,
# every message quoted in docs/FAQ.md is in the sources, every ZAMIN_* CMake option is in docs/BuildAndExport.md,
# the unit test catalogue of docs/Testing.md is current, CHANGELOG.md matches the git history (Tests/changelog.py),
# docs/README.md indexes every document and every image of docs/images is used, <= 400 KB and <= 1280 px wide.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME DocsCheck COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/docs_check.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(DocsCheck PROPERTIES LABELS "docs")
    # The whole script classes of the documentation compile (Tests/docs_snippets.py): with the flags the build uses
    # for the sample projects' scripts (compile_commands.json), -fsyntax-only, warnings count as errors.
    add_test(NAME DocsSnippets COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/docs_snippets.py
        --build ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(DocsSnippets PROPERTIES LABELS "docs")
endif()

# The new-script templates must always compile: generate them and build them with the engine.
add_executable(ZaminScriptTemplateGen ScriptTemplateGen.cpp)
target_link_libraries(ZaminScriptTemplateGen PRIVATE Zamin)
set_target_properties(ZaminScriptTemplateGen PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
zamin_target_settings(ZaminScriptTemplateGen)
set(ZAMIN_TEMPLATE_DIR ${CMAKE_CURRENT_BINARY_DIR}/ScriptTemplates)
set(ZAMIN_TEMPLATE_SOURCES ${ZAMIN_TEMPLATE_DIR}/TemplateFullCheck.cpp ${ZAMIN_TEMPLATE_DIR}/TemplateMinimalCheck.cpp
    ${ZAMIN_TEMPLATE_DIR}/ProjectStarterCheck.cpp)
add_custom_command(OUTPUT ${ZAMIN_TEMPLATE_SOURCES}
    COMMAND ZaminScriptTemplateGen ${ZAMIN_TEMPLATE_DIR}
    DEPENDS ZaminScriptTemplateGen
    COMMENT "Generating the script templates for the compile check"
    VERBATIM)
add_library(ZaminScriptTemplateCheck OBJECT ${ZAMIN_TEMPLATE_SOURCES})
target_link_libraries(ZaminScriptTemplateCheck PRIVATE Zamin)
zamin_target_settings(ZaminScriptTemplateCheck)

# Rendering regression test: renders reference scenes offscreen and compares them with
# Tests/Reference/*.png. It needs a GL context (a display; xvfb-run in CI), so it is not part of
# ctest - run it with the render_tests target (update_render_references writes new references
# after an intended change of the look), Tests/render_tests.sh, or the executable itself.
add_executable(ZaminRenderTests RenderTests.cpp)
target_link_libraries(ZaminRenderTests PRIVATE Zamin ZaminEditorSupport)
target_compile_definitions(ZaminRenderTests PRIVATE ZAMIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    ZAMIN_RENDER_TEST_OUTPUT="${CMAKE_BINARY_DIR}/RenderTests")
set_target_properties(ZaminRenderTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
zamin_target_settings(ZaminRenderTests)
# Tests/render_tests.sh runs it the same way locally and in CI: Mesa's software rasteriser (the references
# are llvmpipe pictures), a fixed xvfb screen without a display, report.json / summary.md and the pictures
# of the failed cases in build/RenderTests.
add_custom_target(render_tests
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/render_tests.sh
    DEPENDS ZaminRenderTests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Rendering regression test (comparison with the reference pictures)"
    USES_TERMINAL VERBATIM)
add_custom_target(update_render_references
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/render_tests.sh --update
    DEPENDS ZaminRenderTests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Regenerating the rendering reference pictures (Tests/Reference)"
    USES_TERMINAL VERBATIM)

# Particle benchmark (not part of ctest): times the particle update headless - about 50 000
# particles per case, the profiler's scopes with --profile. Run build/bin/ZaminBench, or the
# particle_bench target (--nav: the navigation benchmark, the nav_bench target).
add_executable(ZaminBench ParticleBench.cpp NavBench.cpp PrefabBench.cpp RenderBench.cpp EcsBench.cpp SerializeBench.cpp FontBench.cpp CombatBench.cpp
    PhysicsBench.cpp AnimationBench.cpp SliceStressBench.cpp FoliageBench.cpp TerrainTilesBench.cpp GraphBench.cpp)
target_link_libraries(ZaminBench PRIVATE Zamin)
set_target_properties(ZaminBench PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
zamin_target_settings(ZaminBench)
# --serialize saves / loads a large scene in the sample Platformer2D project.
target_compile_definitions(ZaminBench PRIVATE ZAMIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(ZAMIN_OPTIMIZE_PARTICLES_IN_DEBUG)
    target_compile_definitions(ZaminBench PRIVATE ZAMIN_PARTICLES_OPTIMIZED_IN_DEBUG=1)
endif()
add_custom_target(particle_bench
    COMMAND $<TARGET_FILE:ZaminBench>
    DEPENDS ZaminBench
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Particle benchmark (ms / frame)"
    USES_TERMINAL VERBATIM)
# Navigation benchmark: 200 Nav Agents on a 256 x 256 grid - the bakes, the path searches on one thread
# and batched over the job system, ms / frame (ZaminBench --nav).
add_custom_target(nav_bench
    COMMAND $<TARGET_FILE:ZaminBench> --nav
    DEPENDS ZaminBench
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Navigation benchmark (path finding, agents, bake)"
    USES_TERMINAL VERBATIM)

# The headless benchmarks keep working: quick runs at small sizes under ctest (label "bench"; they fail when a
# benchmark's own check fails - what --serialize reads back must be what it saved). The numbers are not checked
# there: Tests/bench.sh --compare does that against Tests/Baselines (a Release build on the baseline's computer).
add_test(NAME BenchEcsSmoke COMMAND ZaminBench --ecs --entities 1000 --frames 5)
add_test(NAME BenchSerializeSmoke COMMAND ZaminBench --serialize --entities 1000)
# The combat world per frame: projectiles (2D rays / 3D sphere casts) and effect books (ZaminBench --combat).
add_test(NAME BenchCombatSmoke COMMAND ZaminBench --combat --frames 5)
# The slices' stress gates (ZaminBench --physics / --animation / --slice-stress) at small sizes: their invariants and
# determinism fail the run (exit code 3), the times are not checked here.
add_test(NAME BenchPhysicsSmoke COMMAND ZaminBench --physics --bodies 200 --actors 12 --frames 40)
add_test(NAME BenchAnimationSmoke COMMAND ZaminBench --animation --humanoids 24 --sprites 64 --frames 30 --jobs 2)
add_test(NAME BenchSliceStressSmoke COMMAND ZaminBench --slice-stress --frames 20)
# Large Visual Script graphs (ZaminBench --graphs) at a small size: every case loads, validates, cooks, runs from the
# source and from the cooked bytecode with the same result, and the VM profiler counts its nodes (exit code 3 otherwise).
add_test(NAME BenchGraphsSmoke COMMAND ZaminBench --graphs --nodes 600 --repeat 2)
set_tests_properties(BenchEcsSmoke BenchSerializeSmoke BenchCombatSmoke BenchPhysicsSmoke BenchAnimationSmoke BenchSliceStressSmoke BenchGraphsSmoke PROPERTIES LABELS "bench" TIMEOUT 600)
add_custom_target(bench
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/bench.sh
    DEPENDS ZaminBench
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Headless benchmarks (ECS, serialization, prefabs) with JSON results"
    USES_TERMINAL VERBATIM)

# Rendering benchmark (a GL context: xvfb-run without a display): 10 000 sprites, 1 000 meshes and both together,
# ms / frame (ZaminBench --render via Tests/render_bench.sh; the baseline is in docs/Benchmarks.md).
add_custom_target(render_bench
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/render_bench.sh
    DEPENDS ZaminBench
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Render benchmark (10k sprite, 1k mesh)"
    USES_TERMINAL VERBATIM)

# Player performance test (not part of ctest: it needs a display - xvfb-run without one): the sample games'
# start scenes in ZaminPlayer --benchmark, ms / frame (average, median, p95, p99) and FPS per game, the reports
# in build/PlayerBench. Run the player_bench target or Tests/player_bench.sh (--frames, --max-avg, --max-p95 ...).
add_custom_target(player_bench
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/player_bench.sh
    DEPENDS ZaminPlayer
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Player benchmark (headless, ms / frame)"
    USES_TERMINAL VERBATIM)
# Automatic export test (needs a display too): the editor exports Platformer2D into a temporary folder, the
# exported game runs headless from its launcher (exit code, log, script module, a screenshot that shows the game,
# a moved folder). Run the export_test target, ctest -L gui or Tests/export_test.sh (--project, --frames, --keep).
add_custom_target(export_test
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/export_test.sh
    DEPENDS ZaminPlayer
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Export test (the editor's export + the exported game headless)"
    USES_TERMINAL VERBATIM)
if(TARGET ZaminEditor)
    add_dependencies(export_test ZaminEditor)
endif()
# The player builds of the three sample games (CI artefacts): each exported, tested as above and packed as
# <Name>-<version>-linux-<arch>.tar.gz + .sha256 in build/PlayerBuilds (Tests/package_games.sh --out DIR).
add_custom_target(package_games
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/package_games.sh
    DEPENDS ZaminPlayer
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Game builds: export, check, tar.gz packages"
    USES_TERMINAL VERBATIM)
if(TARGET ZaminEditor)
    add_dependencies(package_games ZaminEditor)
endif()
foreach(game Platformer2D Showcase3D Adventure3D)
    if(TARGET ${game})
        add_dependencies(player_bench ${game})
        add_dependencies(export_test ${game})
        add_dependencies(package_games ${game})
    endif()
endforeach()

# clang-format on the lines changed against HEAD (.clang-format: the existing style; the code base is not reformatted
# as a whole) and how far the whole code base is from it: Tests/format_check.sh --help.
add_custom_target(format_check
    COMMAND ${PROJECT_SOURCE_DIR}/Tests/format_check.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "clang-format: changed lines"
    USES_TERMINAL VERBATIM)
add_custom_target(format_stats
    COMMAND ${PROJECT_SOURCE_DIR}/Tests/format_check.sh --stats --top 20
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "clang-format: how far the whole code base is from .clang-format"
    USES_TERMINAL VERBATIM)

# The editor smoke test (every window, page, menu and popup for a few frames; errors, OpenGL error flags and
# failed ImGui checks fail it) and the script hot reload test (a module built by the editor, changed, built again
# outside it and reloaded; the field values survive): Tests/editor_smoke.sh, Tests/hot_reload_test.sh.
add_custom_target(editor_smoke
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/editor_smoke.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Editor smoke test (every window, menu and popup)"
    USES_TERMINAL VERBATIM)
add_custom_target(hot_reload_test
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/hot_reload_test.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Skript hot reload testi"
    USES_TERMINAL VERBATIM)
if(TARGET ZaminEditor)
    add_dependencies(editor_smoke ZaminEditor)
    add_dependencies(hot_reload_test ZaminEditor)
endif()
foreach(game Platformer2D Showcase3D)
    if(TARGET ${game})
        add_dependencies(editor_smoke ${game})
    endif()
endforeach()

# The same three in ctest with the label "gui": they run the editor / the exported game (xvfb-run without a
# display) and take minutes - ctest -L gui runs them, ctest -LE gui only the quick ones (CI's unit test step).
if(TARGET ZaminEditor AND TARGET ZaminPlayer AND ZAMIN_BUILD_EXAMPLES)
    add_test(NAME EditorSmoke COMMAND ${PROJECT_SOURCE_DIR}/Tests/editor_smoke.sh WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME ScriptHotReload COMMAND ${PROJECT_SOURCE_DIR}/Tests/hot_reload_test.sh WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME ExportTest COMMAND ${PROJECT_SOURCE_DIR}/Tests/export_test.sh WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # The cooked export: StreamingWorkshop exported, its Player run on cooked textures, an atlas page and streamed tiles,
    # the cook report and cache checked; VisualWorkshop exported with its graphs cooked to bytecode and run by the
    # exported Player (Tests/cook_export_test.sh).
    add_test(NAME ExportCookedTest COMMAND ${PROJECT_SOURCE_DIR}/Tests/cook_export_test.sh WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(EditorSmoke ScriptHotReload ExportTest ExportCookedTest PROPERTIES
        LABELS "gui;slow"
        ENVIRONMENT "ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
        TIMEOUT 1800
        RUN_SERIAL TRUE)
endif()

# The sample games (label "samples"). SampleAssets (quick, no display): the generated games' assets (the six M24 games
# and, in graph mode, the two vertical slices) - budget,
# .zmeta, PNG / WAV, references (Tests/samples_assets.py; --regen compares with the generators' output, needs numpy).
# Sample<Game> (also "gui;slow": ZaminPlayer under xvfb, 1-5 minutes each): its bot demo makes progress (score,
# waves, levels, laps ...) and saves a record the next run reads back, its menus work with keys and with a gamepad,
# it runs without errors and shows a picture (Tests/samples_test.py --game <Game>; logs and screenshots in
# build/SampleTests/<Game>). ctest -L samples runs them, the samples_test target all of them in one go.
# SampleExports ("samples;gui;slow", ~40 minutes): every sample exported by the editor and its exported build played
# headless (Tests/samples_export.py: export_test.sh + samples_test.py --exported; build/SampleExports).
if(Python3_Interpreter_FOUND)
    add_test(NAME SampleAssets COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/samples_assets.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(SampleAssets PROPERTIES LABELS "samples")
    # The two vertical slices in graph mode with --regen: their generators run into a temporary folder and must give
    # the committed projects byte for byte (skipped with a message when numpy is missing).
    add_test(NAME SliceAssets COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/samples_assets.py --game LanternKeeper --game BeaconPass --regen
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(SliceAssets PROPERTIES LABELS "samples;slices" TIMEOUT 300)
endif()
if(TARGET ZaminPlayer AND ZAMIN_BUILD_EXAMPLES AND Python3_Interpreter_FOUND)
    set(ZAMIN_SAMPLE_GAMES TopDownShooter Breakout FPSArena Sokoban EndlessRunner Racing Platformer2D Showcase3D Adventure3D)
    foreach(game ${ZAMIN_SAMPLE_GAMES})
        add_test(NAME Sample${game} COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/samples_test.py --game ${game}
            --out ${CMAKE_BINARY_DIR}/SampleTests/${game}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(Sample${game} PROPERTIES
            LABELS "samples;gui;slow"
            ENVIRONMENT "ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
            TIMEOUT 1800
            RUN_SERIAL TRUE)
    endforeach()
    if(TARGET ZaminEditor)
        add_test(NAME SampleExports COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/samples_export.py
            --out ${CMAKE_BINARY_DIR}/SampleExports
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(SampleExports PROPERTIES
            LABELS "samples;gui;slow"
            ENVIRONMENT "ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
            TIMEOUT 5400
            RUN_SERIAL TRUE)
    endif()
    add_custom_target(samples_test
        COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR}
            ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/samples_test.py --out ${CMAKE_BINARY_DIR}/SampleTests
        DEPENDS ZaminPlayer
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Sample games test (demo bot, menus, headless)"
        USES_TERMINAL VERBATIM)
    foreach(game ${ZAMIN_SAMPLE_GAMES})
        if(TARGET ${game})
            add_dependencies(samples_test ${game})
        endif()
    endforeach()
endif()

# Pixel-oracle terrain shader/cache checks, requires GL (xvfb-run in CI).
add_executable(ZaminTerrainSurfaceTests TerrainSurfaceRenderTests.cpp)
target_link_libraries(ZaminTerrainSurfaceTests PRIVATE Zamin)
set_target_properties(ZaminTerrainSurfaceTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
zamin_target_settings(ZaminTerrainSurfaceTests)

# Real SDK-produced DSOs: no GL needed, constructor gates and transactional hot reload.
foreach(probe ZaminABIGood ZaminABINext ZaminABIBroken)
    zamin_add_game_module(${probe} Fixtures/ModuleABI/Probe.cpp)
    set_target_properties(${probe} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ModuleABI"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/ModuleABI"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/ModuleABI")
    zamin_target_settings(${probe})
endforeach()
target_compile_definitions(ZaminABINext PRIVATE PROBE_VALUE=29)
target_compile_definitions(ZaminABIBroken PRIVATE PROBE_UNRESOLVED=1 PROBE_VALUE=99)
# A module with one object file stamped with another SDK fingerprint (an object not rebuilt after a header change).
zamin_add_game_module(ZaminABIStale Fixtures/ModuleABI/Probe.cpp Fixtures/ModuleABI/Stale.cpp)
set_target_properties(ZaminABIStale PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ModuleABI"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/ModuleABI" LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/ModuleABI")
zamin_target_settings(ZaminABIStale)
target_compile_definitions(ZaminABIStale PRIVATE PROBE_VALUE=55)
set_source_files_properties(Fixtures/ModuleABI/Stale.cpp TARGET_DIRECTORY ZaminABIStale PROPERTIES
    COMPILE_DEFINITIONS "ZAMIN_MODULE_TU_STAMP=\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"")
add_library(ZaminABIMissing SHARED Fixtures/ModuleABI/Probe.cpp)
target_link_libraries(ZaminABIMissing PRIVATE Zamin)
target_compile_definitions(ZaminABIMissing PRIVATE PROBE_VALUE=77)
set_target_properties(ZaminABIMissing PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ModuleABI")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ZaminABIMissing PRIVATE -fno-gnu-unique)
endif()
add_executable(ZaminModuleABITests ModuleABILoadTests.cpp)
target_link_libraries(ZaminModuleABITests PRIVATE Zamin)
zamin_target_settings(ZaminModuleABITests)
add_dependencies(ZaminModuleABITests ZaminABIGood ZaminABINext ZaminABIBroken ZaminABIMissing ZaminABIStale)
add_test(NAME ModuleABILoad COMMAND ZaminModuleABITests $<TARGET_FILE:ZaminABIGood> $<TARGET_FILE:ZaminABINext> $<TARGET_FILE:ZaminABIMissing> $<TARGET_FILE:ZaminABIBroken> $<TARGET_FILE:ZaminABIStale>)

add_executable(ZaminGPUProfilerTests GPUProfilerRenderTests.cpp)
target_link_libraries(ZaminGPUProfilerTests PRIVATE Zamin)
zamin_target_settings(ZaminGPUProfilerTests)

if(Python3_Interpreter_FOUND)
    add_test(NAME SDKManifest COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/sdk_manifest_test.py --build ${CMAKE_BINARY_DIR})
    set_tests_properties(SDKManifest PROPERTIES LABELS "sdk" TIMEOUT 180)
    # An engine header change recompiles every object file of a game module; a module linked with a stale one is refused.
    add_test(NAME ModuleRebuild COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/module_rebuild_test.py --build ${CMAKE_BINARY_DIR})
    set_tests_properties(ModuleRebuild PROPERTIES LABELS "sdk" TIMEOUT 300)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(_zamin_clang_link_probe NAMES clang++ clang++-20 clang++-19 clang++-18 clang++-17)
        if(_zamin_clang_link_probe)
            add_test(NAME SanitizerSharedLink COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/Tests/sanitizer_link_test.py --compiler ${_zamin_clang_link_probe})
            set_tests_properties(SanitizerSharedLink PROPERTIES LABELS "build" TIMEOUT 90 SKIP_RETURN_CODE 77)
        endif()
    endif()
endif()

add_executable(ZaminFoliageRenderTests FoliageRenderTests.cpp)
target_link_libraries(ZaminFoliageRenderTests PRIVATE Zamin)
target_include_directories(ZaminFoliageRenderTests PRIVATE ${PROJECT_SOURCE_DIR}/vendor/glad/include)
zamin_target_settings(ZaminFoliageRenderTests)
# Packed foliage on Mesa llvmpipe (GL 4.5: the GPU compute path, and the GL 4.1 CPU path as the oracle): per-LOD,
# shadow-set and HLOD counts and pictures must match, the HLOD bake/invalidation and the GPU timestamps work.
find_program(ZAMIN_FOLIAGE_XVFB_RUN xvfb-run)
if(ZAMIN_FOLIAGE_XVFB_RUN)
    add_test(NAME FoliageGPUCulling COMMAND ${CMAKE_COMMAND} -E env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
        ${ZAMIN_FOLIAGE_XVFB_RUN} -a -s "-screen 0 1600x900x24 -nolisten tcp" $<TARGET_FILE:ZaminFoliageRenderTests> --require-gpu)
    set_tests_properties(FoliageGPUCulling PROPERTIES LABELS "gui;render" TIMEOUT 300)
    # The dense foliage benchmark at a small size (4 components, 5 frames): every case runs and the CPU and GPU paths end
    # with the same per-LOD and shadow counts (the numbers themselves: Tests/foliage_bench.sh, docs/Benchmarks.md).
    add_test(NAME BenchFoliageSmoke COMMAND ${CMAKE_COMMAND} -E env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
        ${ZAMIN_FOLIAGE_XVFB_RUN} -a -s "-screen 0 1600x900x24 -nolisten tcp" $<TARGET_FILE:ZaminBench> --foliage --instances 262144 --frames 5 --size 640x360)
    set_tests_properties(BenchFoliageSmoke PROPERTIES LABELS "gui;bench" TIMEOUT 600)
    # The terrain tile streaming benchmark at a small size (6 x 6 tiles of 65 samples, 20 frames): every case runs,
    # resident memory stays within the budget and resident neighbours share every edge sample (no cracks).
    add_test(NAME BenchTerrainTilesSmoke COMMAND ${CMAKE_COMMAND} -E env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
        ${ZAMIN_FOLIAGE_XVFB_RUN} -a -s "-screen 0 1600x900x24 -nolisten tcp" $<TARGET_FILE:ZaminBench> --terrain-tiles --tiles 6 --resolution 65 --frames 20 --size 640x360)
    set_tests_properties(BenchTerrainTilesSmoke PROPERTIES LABELS "gui;bench" TIMEOUT 600)
endif()


if(Python3_Interpreter_FOUND AND TARGET WorldWorkshop3D AND TARGET ZaminEditor AND TARGET ZaminPlayer)
    add_test(NAME WorldStreamingRuntime COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_streaming_test.py
        --build ${CMAKE_BINARY_DIR} --out ${CMAKE_BINARY_DIR}/WorldStreamingRuntime)
    set_tests_properties(WorldStreamingRuntime PROPERTIES LABELS "gui;world" TIMEOUT 600)
endif()

# Streamed cell assets on real Player runs along the repeatable camera tour (Tools/make_world_gallery.py
# generates the heavy worlds): hard decode / upload budgets and the frame-time gate, residency limits with
# fast camera / return / damaged cells, and the committed WorldGallery3D demo with export.
if(Python3_Interpreter_FOUND AND TARGET ZaminEditor AND TARGET ZaminPlayer)
    foreach(stage budget residency demo)
        add_test(NAME WorldAssetStreaming_${stage} COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_asset_streaming_test.py
            --build ${CMAKE_BINARY_DIR} --out ${CMAKE_BINARY_DIR}/WorldAssetStreaming/${stage} --stage ${stage})
        set_tests_properties(WorldAssetStreaming_${stage} PROPERTIES LABELS "gui;world;slow" TIMEOUT 1800 RUN_SERIAL TRUE)
    endforeach()
endif()

# GL ownership / allocation accounting for residency eviction; the unit planner tests stay headless.
add_executable(ZaminResidencyGPUTests ResidencyGPUTests.cpp)
target_link_libraries(ZaminResidencyGPUTests PRIVATE Zamin)
set_target_properties(ZaminResidencyGPUTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
target_include_directories(ZaminResidencyGPUTests PRIVATE ${PROJECT_SOURCE_DIR}/vendor/glad/include)
zamin_target_settings(ZaminResidencyGPUTests)
if(Python3_Interpreter_FOUND)
    add_test(NAME WorldModuleLocations COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_module_locations_test.py)
endif()
find_program(ZAMIN_TEST_XVFB_RUN xvfb-run)
if(ZAMIN_TEST_XVFB_RUN)
    add_test(NAME AssetResidencyGPU COMMAND ${CMAKE_COMMAND} -E env LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 ZAMIN_GL_DEBUG=1
        ${ZAMIN_TEST_XVFB_RUN} -a $<TARGET_FILE:ZaminResidencyGPUTests>)
    set_tests_properties(AssetResidencyGPU PROPERTIES LABELS "gui" TIMEOUT 90)
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME WorldCheckpointRuntime COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_checkpoint_test.py
        --build ${CMAKE_BINARY_DIR} --out ${CMAKE_BINARY_DIR}/WorldCheckpointRuntime)
    set_tests_properties(WorldCheckpointRuntime PROPERTIES LABELS "gui;slow" TIMEOUT 1200)
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME WorldNavigationRuntime COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_navigation_test.py
        --build ${CMAKE_BINARY_DIR} --out ${CMAKE_BINARY_DIR}/WorldNavigationRuntime)
    set_tests_properties(WorldNavigationRuntime PROPERTIES LABELS "gui;world" TIMEOUT 700)
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME WorldAgentRuntime COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_agent_test.py
        --build ${CMAKE_BINARY_DIR} --out ${CMAKE_BINARY_DIR}/WorldAgentRuntime)
    set_tests_properties(WorldAgentRuntime PROPERTIES LABELS "gui;world" TIMEOUT 900)
endif()

if(Python3_Interpreter_FOUND AND UNIX)
    add_test(NAME WorldCrowdRuntime COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_crowd_test.py
        --build ${CMAKE_BINARY_DIR} --out ${CMAKE_BINARY_DIR}/WorldCrowdRuntime)
    set_tests_properties(WorldCrowdRuntime PROPERTIES LABELS "gui;world" TIMEOUT 1200)
endif()

if(Python3_Interpreter_FOUND AND UNIX)
    add_test(NAME LinuxDependencyInstaller COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/install_linux_dependencies_test.py)
endif()

if(Python3_Interpreter_FOUND AND TARGET WorldWorkshop3D AND TARGET WorldWorkshop2D)
    add_test(NAME WorldRecoveryRuntime COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Tests/world_recovery_test.py
        --build ${CMAKE_BINARY_DIR} --out ${CMAKE_BINARY_DIR}/WorldRecoveryRuntime)
    set_tests_properties(WorldRecoveryRuntime PROPERTIES LABELS "gui;world" TIMEOUT 1200)
endif()
