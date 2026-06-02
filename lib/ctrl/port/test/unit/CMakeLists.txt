add_executable(ddcs_ctrl_port_transport_test ddcs_ctrl_port_transport_test.cpp)
target_link_libraries(
    ddcs_ctrl_port_transport_test
    PRIVATE compile_option ddcs::ctrl::port GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_port_transport_test
    COMMAND ddcs_ctrl_port_transport_test
)
