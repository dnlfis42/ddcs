add_executable(ddcs_proto_msg_message_test ddcs_proto_msg_message_test.cpp)
target_link_libraries(
    ddcs_proto_msg_message_test
    PRIVATE compile_option ddcs::proto::msg GTest::gtest_main
)
add_test(NAME ddcs_proto_msg_message_test COMMAND ddcs_proto_msg_message_test)
