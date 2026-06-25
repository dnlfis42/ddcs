ddcs_add_unit_test(
    ddcs_agent_domain_dummy_device_test
    SOURCES ddcs_agent_domain_dummy_device_test.cpp
    LIBRARIES ddcs::device ddcs::agent::domain
)

ddcs_add_unit_test(
    ddcs_agent_domain_simulated_device_test
    SOURCES ddcs_agent_domain_simulated_device_test.cpp
    LIBRARIES ddcs::device ddcs::agent::domain
)
