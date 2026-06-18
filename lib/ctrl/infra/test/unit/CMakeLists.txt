ddcs_add_unit_test(
    ddcs_ctrl_infra_frame_server_test
    SOURCES ddcs_ctrl_infra_frame_server_test.cpp
    LIBRARIES
        ddcs::ctrl::infra
        ddcs::io
        ddcs::wire::frame
)

ddcs_add_unit_test(
    ddcs_ctrl_infra_prometheus_server_test
    SOURCES ddcs_ctrl_infra_prometheus_server_test.cpp
    LIBRARIES ddcs::ctrl::infra ddcs::io
)
