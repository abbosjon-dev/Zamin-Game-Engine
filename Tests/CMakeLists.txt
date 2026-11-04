add_executable(ZaminTests ZaminTests.cpp)
target_link_libraries(ZaminTests PRIVATE Zamin)
set_target_properties(ZaminTests PROPERTIES BUILD_RPATH "${CMAKE_BINARY_DIR}/bin")
add_test(NAME ZaminTests COMMAND ZaminTests)
