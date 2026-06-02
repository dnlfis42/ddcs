add_executable(
    ddcs_agent_infra_backoff_schedule_test
    ddcs_agent_infra_backoff_schedule_test.cpp
)
target_link_libraries(
    ddcs_agent_infra_backoff_schedule_test
    PRIVATE compile_option ddcs::agent::infra GTest::gtest_main
)
add_test(
    NAME ddcs_agent_infra_backoff_schedule_test
    COMMAND ddcs_agent_infra_backoff_schedule_test
)

add_executable(
    ddcs_agent_infra_connection_test
    ddcs_agent_infra_connection_test.cpp
)
target_link_libraries(
    ddcs_agent_infra_connection_test
    PRIVATE compile_option ddcs::agent::infra ddcs::common GTest::gtest_main
)
add_test(
    NAME ddcs_agent_infra_connection_test
    COMMAND ddcs_agent_infra_connection_test
)
