ddcs_add_unit_test(
    ddcs_ctrl_infra_prometheus_server_test
    SOURCES ddcs_ctrl_infra_prometheus_server_test.cpp
    LIBRARIES ddcs::io ddcs::ctrl::infra
)

ddcs_add_unit_test(
    ddcs_ctrl_infra_transport_server_test
    SOURCES ddcs_ctrl_infra_transport_server_test.cpp
    LIBRARIES ddcs::io ddcs::wire::frame ddcs::ctrl::infra
)
