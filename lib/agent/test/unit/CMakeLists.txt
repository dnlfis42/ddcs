add_executable(ddcs_agent_agent_test ddcs_agent_agent_test.cpp)
target_link_libraries(
    ddcs_agent_agent_test
    PRIVATE
        compile_option
        ddcs::agent
        ddcs::agent::app
        ddcs::agent::domain
        GTest::gtest_main
)
add_test(NAME ddcs_agent_agent_test COMMAND ddcs_agent_agent_test)
