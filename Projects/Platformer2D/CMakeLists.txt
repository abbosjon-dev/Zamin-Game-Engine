# Zamin o'yin loyihasi: C++ skriptlar moduli (libPlatformer2D.so)
cmake_minimum_required(VERSION 3.20)
project(Platformer2D LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Engine ichida build qilinganda Zamin target mavjud; aks holda
# -DZamin_DIR=<engine build papkasi> orqali topiladi (Editor buni avtomatik beradi).
if(NOT TARGET Zamin)
    find_package(Zamin REQUIRED CONFIG)
endif()

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
zamin_add_game_module(Platformer2D ${GAME_SOURCES})
# The sample games are built with the engine's own warnings (cmake/ZaminGame.cmake).
zamin_target_settings(Platformer2D)
