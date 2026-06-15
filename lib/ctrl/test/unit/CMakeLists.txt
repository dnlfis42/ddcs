add_executable(ddcs_ctrl_controller_test ddcs_ctrl_controller_test.cpp)
target_link_libraries(
    ddcs_ctrl_controller_test
    PRIVATE compile_option ddcs::ctrl GTest::gtest_main
)
add_test(NAME ddcs_ctrl_controller_test COMMAND ddcs_ctrl_controller_test)

# PARKED: 이 e2e는 구 proto wire를 직접 말하므로 통과 불가.
# controller/agent 모두 wire(acmp) 3-way로 이행됐으니 신 프로토콜로 재작성 후 활성화한다.
# add_executable(ddcs_ctrl_controller_e2e_test ddcs_ctrl_controller_e2e_test.cpp)
# target_link_libraries(
#     ddcs_ctrl_controller_e2e_test
#     PRIVATE compile_option ddcs::ctrl ddcs::common ddcs::proto::frame ddcs::proto::msg GTest::gtest_main
# )
# add_test(NAME ddcs_ctrl_controller_e2e_test COMMAND ddcs_ctrl_controller_e2e_test)
