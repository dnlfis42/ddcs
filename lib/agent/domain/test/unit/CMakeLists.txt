add_executable(
    ddcs_agent_domain_dummy_device_test
    ddcs_agent_domain_dummy_device_test.cpp
)
target_link_libraries(
    ddcs_agent_domain_dummy_device_test
    PRIVATE
        compile_option
        ddcs::agent::domain
        ddcs::proto::cmd
        GTest::gtest_main
)
add_test(
    NAME ddcs_agent_domain_dummy_device_test
    COMMAND ddcs_agent_domain_dummy_device_test
)

add_executable(
    ddcs_agent_domain_simulated_device_test
    ddcs_agent_domain_simulated_device_test.cpp
)
target_link_libraries(
    ddcs_agent_domain_simulated_device_test
    PRIVATE
        compile_option
        ddcs::agent::domain
        ddcs::proto::cmd
        GTest::gtest_main
)
add_test(
    NAME ddcs_agent_domain_simulated_device_test
    COMMAND ddcs_agent_domain_simulated_device_test
)
