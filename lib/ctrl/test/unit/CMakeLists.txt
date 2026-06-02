add_executable(ddcs_ctrl_controller_test ddcs_ctrl_controller_test.cpp)
target_link_libraries(
    ddcs_ctrl_controller_test
    PRIVATE compile_option ddcs::ctrl GTest::gtest_main
)
add_test(NAME ddcs_ctrl_controller_test COMMAND ddcs_ctrl_controller_test)

add_executable(ddcs_ctrl_controller_e2e_test ddcs_ctrl_controller_e2e_test.cpp)
target_link_libraries(
    ddcs_ctrl_controller_e2e_test
    PRIVATE
        compile_option
        ddcs::ctrl
        ddcs::common
        ddcs::proto::frame
        ddcs::proto::msg
        GTest::gtest_main
)
add_test(
    NAME ddcs_ctrl_controller_e2e_test
    COMMAND ddcs_ctrl_controller_e2e_test
)
