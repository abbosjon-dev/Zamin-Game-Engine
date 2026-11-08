# Code coverage builds: cmake -B build-cov -DZAMIN_COVERAGE=ON (Tests/coverage.sh configures, builds, runs the
# unit tests and writes the report per module with Tests/coverage_report.py).
#
# Only our own targets are instrumented (zamin_target_settings in ZaminGame.cmake: the engine, editor, player,
# tests, benchmarks and the sample game modules); the vendor libraries are not, so the report is about our code.
# gcov's counters go to .gcda files next to the object files when an instrumented program exits. -O0 keeps the
# line counts true to the source in any build type, and the counters are updated atomically: the job system's
# worker threads run engine code too.
#
# Like a sanitizer build, a coverage build writes the sample games' script modules to <build>/GameModules (not to
# the projects' Binaries folders, where the regular build's modules are) and its programs load them from there;
# the modules its editor builds (find_package(Zamin): <build>/ZaminConfig.cmake) go there too, with the counters.
option(ZAMIN_COVERAGE "Code coverage (gcov) for our targets" OFF)

set(ZAMIN_COVERAGE_FLAGS "")
set(ZAMIN_COVERAGE_LINK_FLAGS "")
if(ZAMIN_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ZAMIN_COVERAGE: faqat GCC va Clang qo'llab-quvvatlanadi (${CMAKE_CXX_COMPILER_ID})")
    endif()
    if(ZAMIN_SANITIZE)
        message(FATAL_ERROR "ZAMIN_COVERAGE va ZAMIN_SANITIZE birga emas: alohida build papkalarida")
    endif()
    set(ZAMIN_COVERAGE_FLAGS --coverage -O0 -g -fprofile-update=atomic)
    set(ZAMIN_COVERAGE_LINK_FLAGS --coverage)
    if(NOT ZAMIN_GAME_MODULE_OUTPUT_DIR)
        set(ZAMIN_GAME_MODULE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/GameModules)
    endif()
    message(STATUS "Zamin: coverage build (gcov), o'yin modullari: ${ZAMIN_GAME_MODULE_OUTPUT_DIR}")
endif()
