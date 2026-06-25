ddcs_add_unit_test(
    ddcs_agent_controller_e2e_test
    SOURCES ddcs_agent_controller_e2e_test.cpp
    LIBRARIES
        ddcs::common
        ddcs::logger
        ddcs::device
        ddcs::agent::domain
        ddcs::agent
        ddcs::ctrl
)
