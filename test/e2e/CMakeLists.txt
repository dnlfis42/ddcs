# PARKED: controller가 신 dacp 3-way(RegisterOutcome/RegisterAck)로 전환됨. agent(client)는 아직 구 proto라
# 프로토콜이 맞지 않아 이 e2e는 통과 불가. agent dacp 이행 트랙 완료 후 신 프로토콜로 재작성한다.
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
