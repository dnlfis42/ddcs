ddcs_add_unit_test(
    ddcs_agent_app_session_service_test
    SOURCES ddcs_agent_app_session_service_test.cpp
    LIBRARIES
        ddcs::agent::app
        ddcs::agent::domain
        ddcs::wire::acmp
        ddcs::device
        ddcs::common
)

ddcs_add_unit_test(
    ddcs_agent_port_test
    SOURCES ddcs_agent_port_test.cpp
    LIBRARIES ddcs::agent::app ddcs::common
)
