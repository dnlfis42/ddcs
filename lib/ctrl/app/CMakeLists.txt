add_library(ddcs_ctrl_app STATIC)
add_library(ddcs::ctrl::app ALIAS ddcs_ctrl_app)

# 6a app::session, 6b app::agent. transport 는 6c 에서 추가.
target_sources(
    ddcs_ctrl_app
    PRIVATE
        src/agent/command_service.cpp
        src/agent/register_service.cpp
        src/agent/status_service.cpp
        src/metrics/metrics_service.cpp
        src/ops/operator_service.cpp
        src/policy/policy_service.cpp
        src/session/session_registry.cpp
        src/transport/dispatcher.cpp
        src/transport/liveness_monitor.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/app/agent/command_service.hpp
                include/ddcs/ctrl/app/agent/register_service.hpp
                include/ddcs/ctrl/app/agent/status_service.hpp
                include/ddcs/ctrl/app/metrics/metrics_service.hpp
                include/ddcs/ctrl/app/ops/operator_service.hpp
                include/ddcs/ctrl/app/policy/policy_service.hpp
                include/ddcs/ctrl/app/session/session.hpp
                include/ddcs/ctrl/app/session/session_registry.hpp
                include/ddcs/ctrl/app/transport/dispatcher.hpp
                include/ddcs/ctrl/app/transport/liveness_monitor.hpp
)
target_link_libraries(
    ddcs_ctrl_app
    PRIVATE compile_option ddcs::logger ddcs::proto::msg
    PUBLIC
        ddcs::common
        ddcs::ctrl::domain
        ddcs::ctrl::port
        ddcs::proto::cmd
        ddcs::device
        ddcs::json
)

# 테스트 경로
add_subdirectory(test/unit)
