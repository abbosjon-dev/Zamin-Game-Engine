add_executable(ZaminTests ZaminTests.cpp DebugDrawShippingCheck.cpp)
target_link_libraries(ZaminTests PRIVATE Zamin)
# The sample projects are read by some tests (their assets must stay valid).
target_compile_definitions(ZaminTests PRIVATE ZAMIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(ZaminTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
zamin_target_settings(ZaminTests)
add_test(NAME ZaminTests COMMAND ZaminTests)

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
    COMMENT "Skript shablonlari kompilyatsiya tekshiruvi uchun yaratilmoqda"
    VERBATIM)
add_library(ZaminScriptTemplateCheck OBJECT ${ZAMIN_TEMPLATE_SOURCES})
target_link_libraries(ZaminScriptTemplateCheck PRIVATE Zamin)
zamin_target_settings(ZaminScriptTemplateCheck)

# Rendering regression test: renders reference scenes offscreen and compares them with
# Tests/Reference/*.png. It needs a GL context (a display; xvfb-run in CI), so it is not part of
# ctest - run it with the render_tests target (update_render_references writes new references
# after an intended change of the look), Tests/render_tests.sh, or the executable itself.
add_executable(ZaminRenderTests RenderTests.cpp)
target_link_libraries(ZaminRenderTests PRIVATE Zamin)
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
    COMMENT "Rendering regressiya testi (etalon rasmlar bilan solishtirish)"
    USES_TERMINAL VERBATIM)
add_custom_target(update_render_references
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/render_tests.sh --update
    DEPENDS ZaminRenderTests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Rendering etalon rasmlari qayta yaratilmoqda (Tests/Reference)"
    USES_TERMINAL VERBATIM)

# Particle benchmark (not part of ctest): times the particle update headless - about 50 000
# particles per case, the profiler's scopes with --profile. Run build/bin/ZaminBench, or the
# particle_bench target (--nav: the navigation benchmark, the nav_bench target).
add_executable(ZaminBench ParticleBench.cpp NavBench.cpp PrefabBench.cpp RenderBench.cpp)
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
    COMMENT "Particle benchmark (ms / kadr)"
    USES_TERMINAL VERBATIM)
# Navigation benchmark: 200 Nav Agents on a 256 x 256 grid - the bakes, the path searches on one thread
# and batched over the job system, ms / frame (ZaminBench --nav).
add_custom_target(nav_bench
    COMMAND $<TARGET_FILE:ZaminBench> --nav
    DEPENDS ZaminBench
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Navigatsiya benchmark (yo'l qidirish, agentlar, bake)"
    USES_TERMINAL VERBATIM)

# Rendering benchmark (a GL context: xvfb-run without a display): 10 000 sprites, 1 000 meshes and both together,
# ms / frame (ZaminBench --render via Tests/render_bench.sh; the baseline is in docs/benchmarks.md).
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
    COMMENT "Player ishlash testi (headless, ms / kadr)"
    USES_TERMINAL VERBATIM)
# Automatic export test (needs a display too): the editor exports Platformer2D into a temporary folder, the
# exported game runs headless from its launcher (exit code, log, script module, a screenshot that shows the game,
# a moved folder). Run the export_test target, ctest -L gui or Tests/export_test.sh (--project, --frames, --keep).
add_custom_target(export_test
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/export_test.sh
    DEPENDS ZaminPlayer
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Eksport testi (editor eksporti + eksport qilingan o'yin headless)"
    USES_TERMINAL VERBATIM)
if(TARGET ZaminEditor)
    add_dependencies(export_test ZaminEditor)
endif()
foreach(game Platformer2D Showcase3D Adventure3D)
    if(TARGET ${game})
        add_dependencies(player_bench ${game})
        add_dependencies(export_test ${game})
    endif()
endforeach()

# The editor smoke test (every window, page, menu and popup for a few frames; errors, OpenGL error flags and
# failed ImGui checks fail it) and the script hot reload test (a module built by the editor, changed, built again
# outside it and reloaded; the field values survive): Tests/editor_smoke.sh, Tests/hot_reload_test.sh.
add_custom_target(editor_smoke
    COMMAND ${CMAKE_COMMAND} -E env ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR} ${PROJECT_SOURCE_DIR}/Tests/editor_smoke.sh
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Editor smoke testi (barcha oynalar, menyular, popup'lar)"
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
    set_tests_properties(EditorSmoke ScriptHotReload ExportTest PROPERTIES
        LABELS "gui;slow"
        ENVIRONMENT "ZAMIN_BUILD_DIR=${CMAKE_BINARY_DIR}"
        TIMEOUT 1800
        RUN_SERIAL TRUE)
endif()
