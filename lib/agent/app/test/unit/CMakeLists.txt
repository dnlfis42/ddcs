ddcs_add_unit_test(
    ddcs_agent_app_session_session_service_test
    SOURCES ddcs_agent_app_session_session_service_test.cpp
    LIBRARIES
        ddcs::common
        ddcs::device
        ddcs::wire::message
        ddcs::agent::domain
        ddcs::agent::app
)

ddcs_add_unit_test(
    ddcs_agent_app_transport_port_test
    SOURCES ddcs_agent_app_transport_port_test.cpp
    LIBRARIES ddcs::common ddcs::agent::app
)
