add_executable(ddcs_proto_cmd_command_test ddcs_proto_cmd_command_test.cpp)
target_link_libraries(
    ddcs_proto_cmd_command_test
    PRIVATE compile_option ddcs::proto::cmd GTest::gtest_main
)
add_test(NAME ddcs_proto_cmd_command_test COMMAND ddcs_proto_cmd_command_test)
