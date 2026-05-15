add_executable(
    ddcs_protocol_message_message_test
    ddcs_protocol_message_message_test.cpp
)
target_link_libraries(
    ddcs_protocol_message_message_test
    PRIVATE compile_option ddcs::protocol::message GTest::gtest_main
)
add_test(
    NAME ddcs_protocol_message_message_test
    COMMAND ddcs_protocol_message_message_test
)
