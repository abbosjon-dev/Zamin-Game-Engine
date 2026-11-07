add_executable(ZaminTests ZaminTests.cpp DebugDrawShippingCheck.cpp)
target_link_libraries(ZaminTests PRIVATE Zamin)
# The sample projects are read by some tests (their assets must stay valid).
target_compile_definitions(ZaminTests PRIVATE ZAMIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(ZaminTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
add_test(NAME ZaminTests COMMAND ZaminTests)

# The new-script templates must always compile: generate them and build them with the engine.
add_executable(ZaminScriptTemplateGen ScriptTemplateGen.cpp)
target_link_libraries(ZaminScriptTemplateGen PRIVATE Zamin)
set_target_properties(ZaminScriptTemplateGen PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
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
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ZaminScriptTemplateCheck PRIVATE -Wall -Wextra)
endif()

# Rendering regression test: renders reference scenes offscreen and compares them with
# Tests/Reference/*.png. It needs a GL context (a display; xvfb-run in CI), so it is not part of
# ctest - run it with the render_tests target (update_render_references writes new references
# after an intended change of the look), Tests/render_tests.sh, or the executable itself.
add_executable(ZaminRenderTests RenderTests.cpp)
target_link_libraries(ZaminRenderTests PRIVATE Zamin)
target_compile_definitions(ZaminRenderTests PRIVATE ZAMIN_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    ZAMIN_RENDER_TEST_OUTPUT="${CMAKE_BINARY_DIR}/RenderTests")
set_target_properties(ZaminRenderTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
find_program(ZAMIN_XVFB_RUN xvfb-run)
set(ZAMIN_RENDER_TEST_RUNNER "")
if(ZAMIN_XVFB_RUN)
    set(ZAMIN_RENDER_TEST_RUNNER ${ZAMIN_XVFB_RUN} -a -s "-screen 0 1280x720x24")
endif()
add_custom_target(render_tests
    COMMAND ${ZAMIN_RENDER_TEST_RUNNER} $<TARGET_FILE:ZaminRenderTests>
    DEPENDS ZaminRenderTests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Rendering regressiya testi (etalon rasmlar bilan solishtirish)"
    USES_TERMINAL VERBATIM)
add_custom_target(update_render_references
    COMMAND ${ZAMIN_RENDER_TEST_RUNNER} $<TARGET_FILE:ZaminRenderTests> --update
    DEPENDS ZaminRenderTests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Rendering etalon rasmlari qayta yaratilmoqda (Tests/Reference)"
    USES_TERMINAL VERBATIM)

# Particle benchmark (not part of ctest): times the particle update headless - about 50 000
# particles per case, the profiler's scopes with --profile. Run build/bin/ZaminBench, or the
# particle_bench target.
add_executable(ZaminBench ParticleBench.cpp)
target_link_libraries(ZaminBench PRIVATE Zamin)
set_target_properties(ZaminBench PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
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
# exported game runs headless from its launcher (exit code, log, script module, a moved folder).
# Run the export_test target or Tests/export_test.sh (--project, --frames, --keep).
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
