ddcs_add_unit_test(
    ddcs_agent_infra_backoff_schedule_test
    SOURCES ddcs_agent_infra_backoff_schedule_test.cpp
    LIBRARIES ddcs::agent::infra
)

ddcs_add_unit_test(
    ddcs_agent_infra_connection_test
    SOURCES ddcs_agent_infra_connection_test.cpp
    LIBRARIES ddcs::agent::infra ddcs::common
)
