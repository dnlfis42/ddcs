add_executable(ddcs_device_command_test ddcs_device_command_test.cpp)
target_link_libraries(
    ddcs_device_command_test
    PRIVATE compile_option ddcs::common ddcs::device GTest::gtest_main
)
add_test(NAME ddcs_device_command_test COMMAND ddcs_device_command_test)
