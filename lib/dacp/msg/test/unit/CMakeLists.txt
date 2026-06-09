add_executable(ddcs_dacp_msg_message_test ddcs_dacp_msg_message_test.cpp)
target_link_libraries(
    ddcs_dacp_msg_message_test
    PRIVATE compile_option ddcs::dacp::msg GTest::gtest_main
)
add_test(NAME ddcs_dacp_msg_message_test COMMAND ddcs_dacp_msg_message_test)
