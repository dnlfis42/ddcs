ddcs_add_unit_test(
    ddcs_agent_agent_test
    SOURCES ddcs_agent_agent_test.cpp
    LIBRARIES
        ddcs::agent
        ddcs::agent::app
        ddcs::agent::domain
)
