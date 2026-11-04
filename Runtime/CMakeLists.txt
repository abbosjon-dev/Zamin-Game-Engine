add_executable(ZaminPlayer src/ZaminPlayer.cpp)
target_link_libraries(ZaminPlayer PRIVATE Zamin)
set_target_properties(ZaminPlayer PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
