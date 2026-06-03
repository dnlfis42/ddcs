add_executable(
    ddcs_ctrl_app_session_registry_test
    ddcs_ctrl_app_session_registry_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_session_registry_test
    PRIVATE compile_option ddcs::ctrl::app GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_session_registry_test
    COMMAND ddcs_ctrl_app_session_registry_test
)

add_executable(
    ddcs_ctrl_app_register_service_test
    ddcs_ctrl_app_register_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_register_service_test
    PRIVATE compile_option ddcs::ctrl::app ddcs::proto::msg GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_register_service_test
    COMMAND ddcs_ctrl_app_register_service_test
)

add_executable(
    ddcs_ctrl_app_status_service_test
    ddcs_ctrl_app_status_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_status_service_test
    PRIVATE compile_option ddcs::ctrl::app ddcs::proto::msg GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_status_service_test
    COMMAND ddcs_ctrl_app_status_service_test
)

add_executable(
    ddcs_ctrl_app_command_service_test
    ddcs_ctrl_app_command_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_command_service_test
    PRIVATE
        compile_option
        ddcs::ctrl::app
        ddcs::common
        ddcs::proto::msg
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_command_service_test
    COMMAND ddcs_ctrl_app_command_service_test
)

add_executable(
    ddcs_ctrl_app_operator_service_test
    ddcs_ctrl_app_operator_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_operator_service_test
    PRIVATE
        compile_option
        ddcs::ctrl::app
        ddcs::ctrl::domain
        ddcs::common
        ddcs::proto::msg
        ddcs::proto::cmd
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_operator_service_test
    COMMAND ddcs_ctrl_app_operator_service_test
)

add_executable(
    ddcs_ctrl_app_session_manager_test
    ddcs_ctrl_app_session_manager_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_session_manager_test
    PRIVATE
        compile_option
        ddcs::ctrl::app
        ddcs::common
        ddcs::proto::msg
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_session_manager_test
    COMMAND ddcs_ctrl_app_session_manager_test
)

add_executable(
    ddcs_ctrl_app_liveness_monitor_test
    ddcs_ctrl_app_liveness_monitor_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_liveness_monitor_test
    PRIVATE
        compile_option
        ddcs::ctrl::app
        ddcs::ctrl::domain
        ddcs::common
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_liveness_monitor_test
    COMMAND ddcs_ctrl_app_liveness_monitor_test
)

add_executable(
    ddcs_ctrl_app_metrics_service_test
    ddcs_ctrl_app_metrics_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_metrics_service_test
    PRIVATE
        compile_option
        ddcs::ctrl::app
        ddcs::common
        ddcs::proto::msg
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_metrics_service_test
    COMMAND ddcs_ctrl_app_metrics_service_test
)

add_executable(
    ddcs_ctrl_app_policy_service_test
    ddcs_ctrl_app_policy_service_test.cpp
)
target_link_libraries(
    ddcs_ctrl_app_policy_service_test
    PRIVATE
        compile_option
        ddcs::ctrl::app
        ddcs::common
        ddcs::proto::msg
        ddcs::proto::cmd
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_app_policy_service_test
    COMMAND ddcs_ctrl_app_policy_service_test
)
