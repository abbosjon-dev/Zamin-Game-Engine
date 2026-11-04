# zamin_add_game_module(<name> <sources...>)
# Builds a game's C++ gameplay scripts as a shared module that the Zamin
# Editor and Zamin Player load at runtime (with hot reload in the editor).
# Output: <project>/Binaries/lib<name>.so
function(zamin_add_game_module name)
    add_library(${name} SHARED ${ARGN})
    if(TARGET Zamin)
        target_link_libraries(${name} PRIVATE Zamin)
    else()
        target_link_libraries(${name} PRIVATE Zamin::Zamin)
    endif()
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Binaries
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/Binaries
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/Binaries
        POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # Without this, GCC's STB_GNU_UNIQUE symbols prevent dlclose() from
        # unloading the module, which breaks hot reload.
        target_compile_options(${name} PRIVATE -fno-gnu-unique)
    endif()
endfunction()
