add_executable(ZaminTests ZaminTests.cpp)
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
