# PARKED: 이 e2e는 구 proto wire를 직접 말하므로 통과 불가.
# controller/agent 모두 wire(acmp) 3-way(RegisterOutcome/RegisterAck)로 이행됐으니 신 프로토콜로 재작성 후 활성화한다.
# add_executable(
#     ddcs_agent_controller_e2e_test
#     ddcs_agent_controller_e2e_test.cpp
# )
# target_link_libraries(
#     ddcs_agent_controller_e2e_test
#     PRIVATE
#         compile_option
#         ddcs::ctrl
#         ddcs::agent
#         ddcs::agent::app
#         ddcs::agent::domain
#         ddcs::common
#         ddcs::proto::cmd
#         GTest::gtest_main
# )
# add_test(
#     NAME ddcs_agent_controller_e2e_test
#     COMMAND ddcs_agent_controller_e2e_test
# )
