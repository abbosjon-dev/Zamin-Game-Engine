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
