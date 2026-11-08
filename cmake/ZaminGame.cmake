# zamin_target_settings(<target>)
# The settings Zamin's own code is built with - the engine, the editor, the player, the tests and tools
# and the sample games' script modules (never the vendor libraries): all of GCC's / Clang's warnings
# (-Wall -Wextra -Wpedantic) and, in a sanitizer build (cmake/Sanitizers.cmake), fatal
# UndefinedBehaviorSanitizer reports. A game's own module can opt in the same way.
#   -Wno-missing-field-initializers: aggregate initialisation of settings / descriptor structs lists the
#   leading members only on purpose - the rest keep their default member initialisers ({ "Ambience",
#   "SFX" } for an AudioGroupSettings; about 70 such places, Clang warns on each).
# -ffp-contract=off: no fused multiply-add contraction, so a GCC and a Clang build compute the same floats
# (Jolt's flags give every target that links it -mfma; GCC's ISO mode never contracts, Clang did by default -
# a rotation's angle to itself was not 0 and the same particle emitter drew other particles).
function(zamin_target_settings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
            -ffp-contract=off)
    endif()
    if(ZAMIN_SANITIZER_STRICT_FLAGS)
        target_compile_options(${target} PRIVATE ${ZAMIN_SANITIZER_STRICT_FLAGS})
    endif()
    # A coverage build (cmake/Coverage.cmake): gcov's counters in our targets, not in the vendor libraries.
    if(ZAMIN_COVERAGE_FLAGS)
        target_compile_options(${target} PRIVATE ${ZAMIN_COVERAGE_FLAGS})
        target_link_options(${target} PRIVATE ${ZAMIN_COVERAGE_LINK_FLAGS})
    endif()
endfunction()

# zamin_add_game_module(<name> <sources...>)
# Builds a game's C++ gameplay scripts as a shared module that the Zamin
# Editor and Zamin Player load at runtime (with hot reload in the editor).
# Output: <project>/Binaries/lib<name>.so - or ZAMIN_GAME_MODULE_OUTPUT_DIR when it is
# set (the editor's export builds a Release module in a build folder of its own there,
# leaving the editor's hot reload module as it is; a sanitizer build of the engine writes
# its instrumented modules to its own GameModules folder).
function(zamin_add_game_module name)
    add_library(${name} SHARED ${ARGN})
    if(TARGET Zamin)
        target_link_libraries(${name} PRIVATE Zamin)
    else()
        target_link_libraries(${name} PRIVATE Zamin::Zamin)
    endif()
    set(output_dir ${CMAKE_CURRENT_SOURCE_DIR}/Binaries)
    if(ZAMIN_GAME_MODULE_OUTPUT_DIR)
        set(output_dir ${ZAMIN_GAME_MODULE_OUTPUT_DIR})
    endif()
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY ${output_dir}
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${output_dir}
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${output_dir}
        POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Without this, GCC's STB_GNU_UNIQUE symbols prevent dlclose() from
        # unloading the module, which breaks hot reload.
        target_compile_options(${name} PRIVATE -fno-gnu-unique)
    endif()
endfunction()
