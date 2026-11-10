file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
add_executable(ZaminEditor ${EDITOR_SOURCES})
target_link_libraries(ZaminEditor PRIVATE Zamin ZaminEditorSupport nlohmann_json::nlohmann_json)
set_target_properties(ZaminEditor PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
zamin_target_settings(ZaminEditor)

# MIT-licensed embedded text widget, editor only; no dependency on Boost.
target_sources(ZaminEditor PRIVATE
    ${PROJECT_SOURCE_DIR}/vendor/ImGuiColorTextEdit/TextEditor.cpp
    ${PROJECT_SOURCE_DIR}/vendor/ImGuiColorTextEdit/LanguageDefinitions.cpp)
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/vendor/ImGuiColorTextEdit/TextEditor.cpp
    ${PROJECT_SOURCE_DIR}/vendor/ImGuiColorTextEdit/LanguageDefinitions.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-variable;-Wno-unused-parameter")
