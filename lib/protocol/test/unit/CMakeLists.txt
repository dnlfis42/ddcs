add_executable(ddcs_protocol_frame_test ddcs_protocol_frame_test.cpp)
target_link_libraries(
    ddcs_protocol_frame_test
    PRIVATE compile_option ddcs::protocol GTest::gtest_main
)
add_test(NAME ddcs_protocol_frame_test COMMAND ddcs_protocol_frame_test)

add_executable(ddcs_protocol_message_test ddcs_protocol_message_test.cpp)
target_link_libraries(
    ddcs_protocol_message_test
    PRIVATE compile_option ddcs::protocol GTest::gtest_main
)
add_test(NAME ddcs_protocol_message_test COMMAND ddcs_protocol_message_test)
