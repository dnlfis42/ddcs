add_executable(ddcs_wire_acmp_message_test ddcs_wire_acmp_message_test.cpp)
target_link_libraries(
    ddcs_wire_acmp_message_test
    PRIVATE compile_option ddcs::wire::acmp GTest::gtest_main
)
add_test(NAME ddcs_wire_acmp_message_test COMMAND ddcs_wire_acmp_message_test)
