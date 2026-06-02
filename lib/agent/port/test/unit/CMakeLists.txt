add_executable(ddcs_agent_port_test ddcs_agent_port_test.cpp)
target_link_libraries(
    ddcs_agent_port_test
    PRIVATE compile_option ddcs::agent::port ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_agent_port_test COMMAND ddcs_agent_port_test)
