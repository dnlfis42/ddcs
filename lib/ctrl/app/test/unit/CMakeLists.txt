add_executable(
    ddcs_ctrl_app_agent_agent_test
    ddcs_ctrl_app_agent_agent_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_agent_agent_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_agent_agent_test
    COMMAND ddcs_ctrl_app_agent_agent_test
)

add_executable(
    ddcs_ctrl_app_agent_agent_service_test
    ddcs_ctrl_app_agent_agent_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_agent_agent_service_test
    PRIVATE compile_option ddcs::ctrl::app ddcs::dacp::msg GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_agent_agent_service_test
    COMMAND ddcs_ctrl_app_agent_agent_service_test
)

add_executable(
    ddcs_ctrl_app_agent_command_sender_test
    ddcs_ctrl_app_agent_command_sender_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_agent_command_sender_test
    PRIVATE compile_option ddcs::ctrl::app ddcs::dacp::frame ddcs::dacp::msg GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_agent_command_sender_test
    COMMAND ddcs_ctrl_app_agent_command_sender_test
)

add_executable(
    ddcs_ctrl_app_agent_device_roster_test
    ddcs_ctrl_app_agent_device_roster_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_agent_device_roster_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_agent_device_roster_test
    COMMAND ddcs_ctrl_app_agent_device_roster_test
)

add_executable(
    ddcs_ctrl_app_agent_handshake_monitor_test
    ddcs_ctrl_app_agent_handshake_monitor_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_agent_handshake_monitor_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_agent_handshake_monitor_test
    COMMAND ddcs_ctrl_app_agent_handshake_monitor_test
)

add_executable(
    ddcs_ctrl_app_agent_liveness_monitor_test
    ddcs_ctrl_app_agent_liveness_monitor_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_agent_liveness_monitor_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_agent_liveness_monitor_test
    COMMAND ddcs_ctrl_app_agent_liveness_monitor_test
)

add_executable(
    ddcs_ctrl_app_agent_registry_test
    ddcs_ctrl_app_agent_registry_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_agent_registry_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_agent_registry_test
    COMMAND ddcs_ctrl_app_agent_registry_test
)

add_executable(
    ddcs_ctrl_app_metrics_service_test
    ddcs_ctrl_app_metrics_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_metrics_service_test
    PRIVATE compile_option ddcs::ctrl::app ddcs::common GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_metrics_service_test
    COMMAND ddcs_ctrl_app_metrics_service_test
)

add_executable(
    ddcs_ctrl_app_device_command_service_test
    ddcs_ctrl_app_device_command_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_device_command_service_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_device_command_service_test
    COMMAND ddcs_ctrl_app_device_command_service_test
)

add_executable(
    ddcs_ctrl_app_device_policy_service_test
    ddcs_ctrl_app_device_policy_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_device_policy_service_test
    PRIVATE compile_option ddcs::ctrl::app ddcs::device GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_device_policy_service_test
    COMMAND ddcs_ctrl_app_device_policy_service_test
)

add_executable(
    ddcs_ctrl_app_device_register_service_test
    ddcs_ctrl_app_device_register_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_device_register_service_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_device_register_service_test
    COMMAND ddcs_ctrl_app_device_register_service_test
)

add_executable(
    ddcs_ctrl_app_device_status_service_test
    ddcs_ctrl_app_device_status_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_device_status_service_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_device_status_service_test
    COMMAND ddcs_ctrl_app_device_status_service_test
)
