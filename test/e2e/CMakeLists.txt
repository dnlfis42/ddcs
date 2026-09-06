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

# main의 profile 수명주기는 library-only 테스트로는 검증할 수 없다. 실제 ctrl을 자식
# 프로세스로 실행해 SIGTERM, boot 실패, dump 실패의 종료 순서를 고정한다.
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
