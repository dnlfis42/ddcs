add_executable(
    ddcs_ctrl_domain_device_registry_test
    ddcs_ctrl_domain_device_registry_test.cpp
)
target_link_libraries(
    ddcs_ctrl_domain_device_registry_test
    PRIVATE compile_option ddcs::ctrl::domain GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_domain_device_registry_test
    COMMAND ddcs_ctrl_domain_device_registry_test
)
