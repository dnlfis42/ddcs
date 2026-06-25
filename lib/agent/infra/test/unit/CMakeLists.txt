ddcs_add_unit_test(
    ddcs_agent_infra_transport_backoff_schedule_test
    SOURCES ddcs_agent_infra_transport_backoff_schedule_test.cpp
    LIBRARIES ddcs::agent::infra
)

ddcs_add_unit_test(
    ddcs_agent_infra_transport_connection_test
    SOURCES ddcs_agent_infra_transport_connection_test.cpp
    LIBRARIES ddcs::common ddcs::agent::infra
)

ddcs_add_unit_test(
    ddcs_agent_infra_transport_connector_test
    SOURCES ddcs_agent_infra_transport_connector_test.cpp
    LIBRARIES ddcs::common ddcs::io ddcs::wire::frame ddcs::agent::infra
)
