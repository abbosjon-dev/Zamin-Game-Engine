# Sanitizer builds: cmake -B build-asan -DZAMIN_SANITIZE=address (or undefined, leak, thread, or a list
# such as "address;undefined" / "address,undefined"). Tests/sanitize.sh configures, builds and runs them.
#
# The flags go to every target of the build - the vendor libraries too (ASan has to see all of the
# process's allocations and stack frames, the standalone LSan replaces malloc for everyone) - and,
# through ZaminConfig.cmake, to game modules built outside this tree (the editor's hot reload, export
# builds). Our own targets (zamin_target_settings in ZaminGame.cmake) additionally make every
# UndefinedBehaviorSanitizer report fatal (-fno-sanitize-recover=all); the vendor libraries' reports
# stay recoverable so Tests/sanitizers/ubsan.supp can silence third-party noise.
#
# A sanitizer build writes the sample games' script modules to <build>/GameModules (ZaminGame.cmake:
# ZAMIN_GAME_MODULE_OUTPUT_DIR), not to the projects' Binaries folders where the regular build's
# modules are - a regular editor cannot load an instrumented module. The build's own programs (<build>/bin) load
# them from there when they exist (Project::GetScriptModulePath; ZAMIN_SCRIPT_MODULE_DIR=<folder> overrides it).
set(ZAMIN_SANITIZE "" CACHE STRING "Sanitizers: address, undefined, leak, thread (a list), empty = none")

set(ZAMIN_SANITIZER_FLAGS "")
set(ZAMIN_SANITIZER_STRICT_FLAGS "")
if(ZAMIN_SANITIZE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ZAMIN_SANITIZE: faqat GCC va Clang qo'llab-quvvatlanadi (${CMAKE_CXX_COMPILER_ID})")
    endif()
    string(REPLACE "," ";" _zm_sanitizers "${ZAMIN_SANITIZE}")
    string(TOLOWER "${_zm_sanitizers}" _zm_sanitizers)
    list(REMOVE_DUPLICATES _zm_sanitizers)
    foreach(_zm_s IN LISTS _zm_sanitizers)
        if(NOT _zm_s MATCHES "^(address|undefined|leak|thread)$")
            message(FATAL_ERROR "ZAMIN_SANITIZE: noma'lum sanitizer '${_zm_s}' (address, undefined, leak, thread)")
        endif()
    endforeach()
    if("thread" IN_LIST _zm_sanitizers AND ("address" IN_LIST _zm_sanitizers OR "leak" IN_LIST _zm_sanitizers))
        message(FATAL_ERROR "ZAMIN_SANITIZE: thread sanitizer address / leak bilan birga ishlamaydi")
    endif()
    # ASan already contains LeakSanitizer (ASAN_OPTIONS=detect_leaks=1): the standalone one is for
    # builds without ASan.
    if("address" IN_LIST _zm_sanitizers)
        list(REMOVE_ITEM _zm_sanitizers leak)
    endif()
    list(JOIN _zm_sanitizers "," _zm_joined)

    # Readable stacks: frame pointers, no tail calls folded away, debug info in every build type.
    set(ZAMIN_SANITIZER_FLAGS -fsanitize=${_zm_joined} -fno-omit-frame-pointer -fno-optimize-sibling-calls -g)
    if("undefined" IN_LIST _zm_sanitizers)
        set(ZAMIN_SANITIZER_STRICT_FLAGS -fno-sanitize-recover=all)
    endif()
    add_compile_options(${ZAMIN_SANITIZER_FLAGS})
    add_link_options(-fsanitize=${_zm_joined})
    set(ZAMIN_SANITIZER_LINK_FLAGS -fsanitize=${_zm_joined})

    if(NOT ZAMIN_GAME_MODULE_OUTPUT_DIR)
        set(ZAMIN_GAME_MODULE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/GameModules)
    endif()
    message(STATUS "Zamin: sanitizer build (${_zm_joined}), o'yin modullari: ${ZAMIN_GAME_MODULE_OUTPUT_DIR}")
endif()
