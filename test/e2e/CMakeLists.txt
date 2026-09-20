ddcs_add_unit_test(
    ddcs_agent_controller_e2e_test
    SOURCES ddcs_agent_controller_e2e_test.cpp
    LIBRARIES
        ddcs::common
        ddcs::logger
        ddcs::device
        ddcs::agent::domain
        ddcs::agent
        ddcs::ctrl
)

ddcs_add_unit_test(
    ddcs_agent_fleet_e2e_test
    SOURCES ddcs_agent_fleet_e2e_test.cpp
    LIBRARIES ddcs::agent ddcs::ctrl
)
target_compile_definitions(
    ddcs_agent_fleet_e2e_test
    PRIVATE
        DDCS_TEST_CONTROLLER_POLICY_PATH="${PROJECT_SOURCE_DIR}/config/controller.json"
)

# ctrl을 자식 프로세스로 실행한다.
# SIGTERM 수신, 초기화 실패, 프로파일 저장 실패 시 프로파일 처리와 프로세스 종료 순서를 검증한다.
ddcs_add_unit_test(
    ddcs_ctrl_profile_lifecycle_e2e_test
    SOURCES ddcs_ctrl_profile_lifecycle_e2e_test.cpp
    LIBRARIES ddcs::profile
)
add_dependencies(ddcs_ctrl_profile_lifecycle_e2e_test ctrl)
target_compile_definitions(
    ddcs_ctrl_profile_lifecycle_e2e_test
    PRIVATE DDCS_CTRL_BINARY="$<TARGET_FILE:ctrl>"
)
