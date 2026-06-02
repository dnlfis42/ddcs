add_executable(
    ddcs_agent_controller_e2e_test
    ddcs_agent_controller_e2e_test.cpp
)
target_link_libraries(
    ddcs_agent_controller_e2e_test
    PRIVATE
        compile_option
        ddcs::ctrl
        ddcs::agent
        ddcs::agent::app
        ddcs::agent::domain
        ddcs::common
        ddcs::proto::cmd
        GTest::gtest_main
)
add_test(
    NAME ddcs_agent_controller_e2e_test
    COMMAND ddcs_agent_controller_e2e_test
)
