ddcs_add_unit_test(
    ddcs_ctrl_app_agent_agent_test
    SOURCES ddcs_ctrl_app_agent_agent_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_agent_agent_service_test
    SOURCES ddcs_ctrl_app_agent_agent_service_test.cpp
    LIBRARIES ddcs::ctrl::app ddcs::wire::acmp
)

ddcs_add_unit_test(
    ddcs_ctrl_app_agent_command_sender_test
    SOURCES ddcs_ctrl_app_agent_command_sender_test.cpp
    LIBRARIES
        ddcs::ctrl::app
        ddcs::wire::acmp
        ddcs::wire::frame
)

ddcs_add_unit_test(
    ddcs_ctrl_app_agent_device_roster_test
    SOURCES ddcs_ctrl_app_agent_device_roster_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_agent_handshake_monitor_test
    SOURCES ddcs_ctrl_app_agent_handshake_monitor_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_agent_liveness_monitor_test
    SOURCES ddcs_ctrl_app_agent_liveness_monitor_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_agent_registry_test
    SOURCES ddcs_ctrl_app_agent_registry_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_metrics_service_test
    SOURCES ddcs_ctrl_app_metrics_service_test.cpp
    LIBRARIES ddcs::ctrl::app ddcs::common
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_command_service_test
    SOURCES ddcs_ctrl_app_device_command_service_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_policy_service_test
    SOURCES ddcs_ctrl_app_device_policy_service_test.cpp
    LIBRARIES ddcs::ctrl::app ddcs::device
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_register_service_test
    SOURCES ddcs_ctrl_app_device_register_service_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_status_service_test
    SOURCES ddcs_ctrl_app_device_status_service_test.cpp
    LIBRARIES ddcs::ctrl::app
)
