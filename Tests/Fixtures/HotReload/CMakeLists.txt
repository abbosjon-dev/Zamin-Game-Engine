# The hot reload test's project (Tests/hot_reload_test.sh copies it into a temporary folder): one C++ script,
# built by the editor (libHotReload.so), then changed and built again while the editor runs.
cmake_minimum_required(VERSION 3.20)
project(HotReload LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT TARGET Zamin)
    find_package(Zamin REQUIRED CONFIG)
endif()

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp)
zamin_add_game_module(HotReload ${GAME_SOURCES})
zamin_target_settings(HotReload)
