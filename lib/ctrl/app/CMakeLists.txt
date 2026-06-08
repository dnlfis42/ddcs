add_library(ddcs_ctrl_app STATIC)
add_library(ddcs::ctrl::app ALIAS ddcs_ctrl_app)

target_sources(
    ddcs_ctrl_app
    PRIVATE
        src/agent/command_service.cpp
        src/agent/register_service.cpp
        src/agent/status_service.cpp
        src/metrics/metrics_service.cpp
        src/ops/operator_service.cpp
        src/policy/policy_service.cpp
        src/session/liveness_monitor.cpp
        src/session/session_manager.cpp
        src/session/session_registry.cpp
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
                include/ddcs/ctrl/app/session/liveness_monitor.hpp
                include/ddcs/ctrl/app/session/session.hpp
                include/ddcs/ctrl/app/session/session_manager.hpp
                include/ddcs/ctrl/app/session/session_registry.hpp
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

add_subdirectory(test/unit)
