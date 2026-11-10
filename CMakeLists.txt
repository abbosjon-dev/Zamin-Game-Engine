cmake_minimum_required(VERSION 3.20)
project(Zamin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(ZAMIN_BUILD_EDITOR  "Build Zamin Editor"          ON)
option(ZAMIN_BUILD_RUNTIME "Build Zamin Player (runtime)" ON)
option(ZAMIN_BUILD_TESTS   "Build Zamin unit tests"      ON)
option(ZAMIN_BUILD_EXAMPLES "Build example game projects" ON)
option(ZAMIN_WITH_IMGUI "Include the ImGui overlay (required by the editor)" ${ZAMIN_BUILD_EDITOR})
option(ZAMIN_ENABLE_AVX2 "Require AVX2/FMA/BMI/F16C CPUs instead of the portable x86-64 baseline" OFF)
if(ZAMIN_BUILD_EDITOR AND NOT ZAMIN_WITH_IMGUI)
    message(FATAL_ERROR "ZAMIN_BUILD_EDITOR requires ZAMIN_WITH_IMGUI=ON")
endif()
# Shipping: the final game build - debug drawing (DebugDraw) and other development aids are
# compiled out. ZAMIN_DEBUG_DRAW: debug drawing in the editor / Development builds.
option(ZAMIN_SHIPPING      "Shipping build (no development aids)" OFF)
option(ZAMIN_DEBUG_DRAW    "DebugDraw API (ignored when ZAMIN_SHIPPING)" ON)
# ZM_ASSERT / ZM_CORE_ASSERT: AUTO - in Debug builds only (NDEBUG decides), ON - also in Release /
# RelWithDebInfo (a "release with asserts" build: a failed assert writes a crash report), OFF - never.
set(ZAMIN_ASSERTS "AUTO" CACHE STRING "Asserts: AUTO (Debug only), ON, OFF")
set_property(CACHE ZAMIN_ASSERTS PROPERTY STRINGS AUTO ON OFF)

# Sanitizers (ZAMIN_SANITIZE=address / undefined / leak / thread) before the dependencies: their flags
# go to every target, the vendor libraries included.
include(cmake/Sanitizers.cmake)
# Code coverage (ZAMIN_COVERAGE=ON: gcov counters in our targets only; Tests/coverage.sh).
include(cmake/Coverage.cmake)
include(cmake/Dependencies.cmake)
# zamin_target_settings (warnings of our own targets), zamin_add_game_module.
include(cmake/ZaminGame.cmake)

add_subdirectory(Zamin)
if(ZAMIN_BUILD_EDITOR)
    add_subdirectory(Editor)
endif()
if(ZAMIN_BUILD_RUNTIME)
    add_subdirectory(Runtime)
endif()
if(ZAMIN_BUILD_EXAMPLES)
    add_subdirectory(Projects/Platformer2D)
    add_subdirectory(Projects/Showcase3D)
    add_subdirectory(Projects/Adventure3D)
    add_subdirectory(Projects/TopDownShooter)
    add_subdirectory(Projects/Breakout)
    add_subdirectory(Projects/FPSArena)
    add_subdirectory(Projects/Sokoban)
    add_subdirectory(Projects/EndlessRunner)
    add_subdirectory(Projects/Racing)
endif()
if(ZAMIN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

# The API reference (Doxygen, the root Doxyfile; docs/BuildAndExport.md): cmake --build build --target docs
# writes <build>/docs/html/index.html and <build>/docs/doxygen-warnings.log. Not part of the default build.
find_package(Doxygen QUIET OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
    # The root Doxyfile with this build's output folder (and no diagrams when Graphviz is missing).
    if(TARGET Doxygen::dot)
        set(ZAMIN_DOXYGEN_DOT "HAVE_DOT = YES\nDOT_PATH = \"${DOXYGEN_DOT_EXECUTABLE}\"")
    else()
        set(ZAMIN_DOXYGEN_DOT "HAVE_DOT = NO")
    endif()
    file(WRITE ${CMAKE_BINARY_DIR}/Doxyfile.build
        "@INCLUDE = \"${PROJECT_SOURCE_DIR}/Doxyfile\"\n"
        "PROJECT_NUMBER = ${PROJECT_VERSION}\n"
        "OUTPUT_DIRECTORY = \"${CMAKE_BINARY_DIR}/docs\"\n"
        "WARN_LOGFILE = \"${CMAKE_BINARY_DIR}/docs/doxygen-warnings.log\"\n"
        "${ZAMIN_DOXYGEN_DOT}\n")
    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/docs
        COMMAND Doxygen::doxygen ${CMAKE_BINARY_DIR}/Doxyfile.build
        COMMAND ${CMAKE_COMMAND} -E echo "API ma'lumotnomasi: ${CMAKE_BINARY_DIR}/docs/html/index.html (ogohlantirishlar: ${CMAKE_BINARY_DIR}/docs/doxygen-warnings.log)"
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Doxygen: API ma'lumotnomasi"
        VERBATIM)
endif()
