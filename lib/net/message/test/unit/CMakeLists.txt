add_executable(ddcs_net_message_message_test ddcs_net_message_message_test.cpp)
target_link_libraries(
    ddcs_net_message_message_test
    PRIVATE compile_option ddcs::net::message GTest::gtest_main
)
add_test(
    NAME ddcs_net_message_message_test
    COMMAND ddcs_net_message_message_test
)
