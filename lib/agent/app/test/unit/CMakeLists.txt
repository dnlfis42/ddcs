add_executable(
    ddcs_agent_app_session_service_test
    ddcs_agent_app_session_service_test.cpp
)
target_link_libraries(
    ddcs_agent_app_session_service_test
    PRIVATE
        compile_option
        ddcs::agent::app
        ddcs::agent::domain
        ddcs::proto::msg
        ddcs::proto::cmd
        ddcs::common
        GTest::gtest_main
)
add_test(
    NAME ddcs_agent_app_session_service_test
    COMMAND ddcs_agent_app_session_service_test
)
