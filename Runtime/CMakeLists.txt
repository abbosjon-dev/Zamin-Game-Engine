add_executable(ZaminPlayer src/ZaminPlayer.cpp)
target_link_libraries(ZaminPlayer PRIVATE Zamin)
set_target_properties(ZaminPlayer PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
# The player's own functions by name in crash reports' backtraces (-rdynamic).
set_target_properties(ZaminPlayer PROPERTIES ENABLE_EXPORTS ON)
zamin_target_settings(ZaminPlayer)
