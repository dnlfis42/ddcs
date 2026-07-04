ddcs_add_unit_test(
    ddcs_ctrl_app_device_command_service_test
    SOURCES ddcs_ctrl_app_device_command_service_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_group_aggregate_test
    SOURCES ddcs_ctrl_app_device_group_aggregate_test.cpp
    LIBRARIES ddcs::device ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_policy_service_test
    SOURCES ddcs_ctrl_app_device_policy_service_test.cpp
    LIBRARIES ddcs::device ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_registration_service_test
    SOURCES ddcs_ctrl_app_device_registration_service_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_device_status_service_test
    SOURCES ddcs_ctrl_app_device_status_service_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_metrics_service_test
    SOURCES ddcs_ctrl_app_metrics_service_test.cpp
    LIBRARIES ddcs::ctrl::app ddcs::common
)

ddcs_add_unit_test(
    ddcs_ctrl_app_session_command_sender_test
    SOURCES ddcs_ctrl_app_session_command_sender_test.cpp
    LIBRARIES
        ddcs::wire::message
        ddcs::wire::frame
        ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_session_device_roster_test
    SOURCES ddcs_ctrl_app_session_device_roster_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_session_registry_test
    SOURCES ddcs_ctrl_app_session_registry_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_session_session_test
    SOURCES ddcs_ctrl_app_session_session_test.cpp
    LIBRARIES ddcs::ctrl::app
)

ddcs_add_unit_test(
    ddcs_ctrl_app_session_session_service_test
    SOURCES ddcs_ctrl_app_session_session_service_test.cpp
    LIBRARIES ddcs::wire::message ddcs::ctrl::app
)
