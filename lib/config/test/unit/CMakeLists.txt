ddcs_add_unit_test(
    ddcs_config_env_test
    SOURCES ddcs_config_env_test.cpp
    LIBRARIES ddcs::config
)

ddcs_add_unit_test(
    ddcs_config_file_test
    SOURCES ddcs_config_file_test.cpp
    LIBRARIES ddcs::config
)
