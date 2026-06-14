add_executable(
    ddcs_ctrl_infra_dacp_server_test
    ddcs_ctrl_infra_dacp_server_test.cpp
)
target_link_libraries(
    ddcs_ctrl_infra_dacp_server_test
    PRIVATE
        compile_option
        ddcs::ctrl::infra
        ddcs::dacp::frame
        ddcs::io
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_infra_dacp_server_test
    COMMAND ddcs_ctrl_infra_dacp_server_test
)

add_executable(
    ddcs_ctrl_infra_prometheus_server_test
    ddcs_ctrl_infra_prometheus_server_test.cpp
)
target_link_libraries(
    ddcs_ctrl_infra_prometheus_server_test
    PRIVATE compile_option ddcs::ctrl::infra ddcs::io GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_infra_prometheus_server_test
    COMMAND ddcs_ctrl_infra_prometheus_server_test
)
