add_executable(
    ddcs_ctrl_infra_metrics_server_test
    ddcs_ctrl_infra_metrics_server_test.cpp
)
target_link_libraries(
    ddcs_ctrl_infra_metrics_server_test
    PRIVATE compile_option ddcs::ctrl::infra ddcs::runtime GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_infra_metrics_server_test
    COMMAND ddcs_ctrl_infra_metrics_server_test
)

add_executable(
    ddcs_ctrl_infra_transport_connection_test
    ddcs_ctrl_infra_transport_connection_test.cpp
)
target_link_libraries(
    ddcs_ctrl_infra_transport_connection_test
    PRIVATE compile_option ddcs::ctrl::infra GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_infra_transport_connection_test
    COMMAND ddcs_ctrl_infra_transport_connection_test
)

add_executable(
    ddcs_ctrl_infra_transport_connection_acceptor_test
    ddcs_ctrl_infra_transport_connection_acceptor_test.cpp
)
target_link_libraries(
    ddcs_ctrl_infra_transport_connection_acceptor_test
    PRIVATE
        compile_option
        ddcs::ctrl::infra
        ddcs::runtime
        ddcs::proto::frame
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_infra_transport_connection_acceptor_test
    COMMAND ddcs_ctrl_infra_transport_connection_acceptor_test
)

add_executable(
    ddcs_ctrl_infra_transport_connection_manager_test
    ddcs_ctrl_infra_transport_connection_manager_test.cpp
)
target_link_libraries(
    ddcs_ctrl_infra_transport_connection_manager_test
    PRIVATE
        compile_option
        ddcs::ctrl::infra
        ddcs::runtime
        ddcs::proto::frame
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_infra_transport_connection_manager_test
    COMMAND ddcs_ctrl_infra_transport_connection_manager_test
)

add_executable(
    ddcs_ctrl_infra_transport_endpoint_test
    ddcs_ctrl_infra_transport_endpoint_test.cpp
)
target_link_libraries(
    ddcs_ctrl_infra_transport_endpoint_test
    PRIVATE compile_option ddcs::ctrl::infra GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_infra_transport_endpoint_test
    COMMAND ddcs_ctrl_infra_transport_endpoint_test
)
