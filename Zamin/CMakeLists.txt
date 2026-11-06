# Zamin engine core (shared library: game script modules link against it).
file(GLOB_RECURSE ZAMIN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Events/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Renderer/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Scene/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/UI/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Scripting/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Physics/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Audio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Debug/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Project/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Asset/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/ImGui/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Math/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Utils/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Platform/GLFW/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Platform/OpenGL/*.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    file(GLOB ZAMIN_PLATFORM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Platform/Linux/*.cpp)
    list(APPEND ZAMIN_SOURCES ${ZAMIN_PLATFORM_SOURCES})
endif()

# Names declared in namespace Zamin: a script class named like one of them would clash with
# `using namespace Zamin;` (the new-script dialog rejects such names).
file(GLOB_RECURSE ZAMIN_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/*.h)
set(ZAMIN_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
set(ZAMIN_ENGINE_NAMES ${ZAMIN_GENERATED_DIR}/ZaminEngineNames.inc)
add_custom_command(OUTPUT ${ZAMIN_ENGINE_NAMES}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin -DOUTPUT=${ZAMIN_ENGINE_NAMES}
            -P ${PROJECT_SOURCE_DIR}/cmake/GenerateEngineNames.cmake
    DEPENDS ${ZAMIN_HEADERS} ${PROJECT_SOURCE_DIR}/cmake/GenerateEngineNames.cmake
    COMMENT "Zamin nomlari ro'yxati (skript klass nomlarini tekshirish uchun)"
    VERBATIM)

add_library(Zamin SHARED ${ZAMIN_SOURCES} ${ZAMIN_ENGINE_NAMES})
add_library(Zamin::Zamin ALIAS Zamin)
target_include_directories(Zamin PRIVATE ${ZAMIN_GENERATED_DIR})

set_target_properties(Zamin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN")

target_include_directories(Zamin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
    $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/misc/cpp>
    $<BUILD_INTERFACE:${imguizmo_SOURCE_DIR}>)

target_compile_definitions(Zamin PUBLIC GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL IMGUI_DEFINE_MATH_OPERATORS)

# Debug drawing (Zamin/Debug/DebugDraw.h): on in the editor and Development builds, compiled out
# (empty inline calls) in Shipping builds. The game modules get the same setting.
set(ZAMIN_BUILD_DEFINITIONS "")
if(ZAMIN_SHIPPING)
    list(APPEND ZAMIN_BUILD_DEFINITIONS ZAMIN_SHIPPING ZM_DEBUG_DRAW=0)
elseif(ZAMIN_DEBUG_DRAW)
    list(APPEND ZAMIN_BUILD_DEFINITIONS ZM_DEBUG_DRAW=1)
else()
    list(APPEND ZAMIN_BUILD_DEFINITIONS ZM_DEBUG_DRAW=0)
endif()
target_compile_definitions(Zamin PUBLIC ${ZAMIN_BUILD_DEFINITIONS})

# Header-only libraries exposed through the public API.
target_link_libraries(Zamin PUBLIC glm::glm EnTT::EnTT)
# Implementation details (compiled into libZamin.so).
target_link_libraries(Zamin PRIVATE glad glfw imgui imguizmo box2d Jolt nlohmann_json::nlohmann_json stb tinyobj cgltf miniaudio
    ${CMAKE_DL_LIBS} pthread)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Zamin PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# The particle simulation and the loops that hand particles to the renderer run for every particle
# every frame: optimised in Debug builds too (about 30x faster there), so effects with tens of
# thousands of particles stay interactive in a Debug editor. The numbers are the same (no
# fast-math). Off: step through those files in a debugger.
option(ZAMIN_OPTIMIZE_PARTICLES_IN_DEBUG "Compile the particle simulation with -O2 in Debug builds" ON)
if(ZAMIN_OPTIMIZE_PARTICLES_IN_DEBUG AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Scene/ParticleSystem.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Zamin/Scene/SceneParticles.cpp
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()

# Copy engine resources (fonts ...) next to the binaries.
add_custom_command(TARGET Zamin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/Resources ${CMAKE_BINARY_DIR}/bin/Resources)

# ---- CMake package so external game projects can build script modules ----
include(${PROJECT_SOURCE_DIR}/cmake/ZaminGame.cmake)

get_target_property(_glm_inc glm::glm INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(_entt_inc EnTT::EnTT INTERFACE_INCLUDE_DIRECTORIES)
set(ZAMIN_EXPORT_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/misc/cpp"
    "${imguizmo_SOURCE_DIR}"
    "${glm_SOURCE_DIR}"
    "${entt_SOURCE_DIR}/src")
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/ZaminConfig.cmake CONTENT
"# Generated by Zamin Engine build. Use: find_package(Zamin CONFIG)
if(NOT TARGET Zamin::Zamin)
    add_library(Zamin::Zamin SHARED IMPORTED)
    set_target_properties(Zamin::Zamin PROPERTIES
        IMPORTED_LOCATION \"$<TARGET_FILE:Zamin>\"
        IMPORTED_SONAME \"$<TARGET_FILE_NAME:Zamin>\"
        INTERFACE_INCLUDE_DIRECTORIES \"${ZAMIN_EXPORT_INCLUDES}\"
        INTERFACE_COMPILE_DEFINITIONS \"GLM_FORCE_RADIANS;GLM_ENABLE_EXPERIMENTAL;IMGUI_DEFINE_MATH_OPERATORS;${ZAMIN_BUILD_DEFINITIONS}\")
endif()
set(ZAMIN_BIN_DIR \"${CMAKE_BINARY_DIR}/bin\")
include(\"${PROJECT_SOURCE_DIR}/cmake/ZaminGame.cmake\")
")
