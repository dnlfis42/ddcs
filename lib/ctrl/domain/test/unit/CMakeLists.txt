ddcs_add_unit_test(
    ddcs_ctrl_domain_device_registry_test
    SOURCES ddcs_ctrl_domain_device_registry_test.cpp
    LIBRARIES ddcs::ctrl::domain
)

ddcs_add_unit_test(
    ddcs_ctrl_domain_group_policy_test
    SOURCES ddcs_ctrl_domain_group_policy_test.cpp
    LIBRARIES ddcs::ctrl::domain
)
