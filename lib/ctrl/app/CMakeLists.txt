add_library(ddcs_ctrl_app STATIC)
add_library(ddcs::ctrl::app ALIAS ddcs_ctrl_app)

target_sources(
    ddcs_ctrl_app
    PRIVATE
        src/agent/agent_service.cpp
        src/agent/command_sender.cpp
        src/agent/device_roster.cpp
        src/agent/handshake_monitor.cpp
        src/agent/liveness_monitor.cpp
        src/device/command_service.cpp
        src/device/policy_service.cpp
        src/device/register_service.cpp
        src/device/status_service.cpp
        src/metrics/metrics_service.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/app/agent/port/connection_id.hpp
                include/ddcs/ctrl/app/agent/port/connection_observer.hpp
                include/ddcs/ctrl/app/agent/port/disconnect_reason.hpp
                include/ddcs/ctrl/app/agent/port/disconnector.hpp
                include/ddcs/ctrl/app/agent/port/message_buffer.hpp
                include/ddcs/ctrl/app/agent/port/message_sender.hpp
                include/ddcs/ctrl/app/agent/agent.hpp
                include/ddcs/ctrl/app/agent/agent_registry.hpp
                include/ddcs/ctrl/app/agent/agent_service.hpp
                include/ddcs/ctrl/app/agent/command_sender.hpp
                include/ddcs/ctrl/app/agent/device_roster.hpp
                include/ddcs/ctrl/app/agent/handshake_monitor.hpp
                include/ddcs/ctrl/app/agent/liveness_monitor.hpp
                include/ddcs/ctrl/app/device/port/command_buffer.hpp
                include/ddcs/ctrl/app/device/port/command_id.hpp
                include/ddcs/ctrl/app/device/port/command_sender.hpp
                include/ddcs/ctrl/app/device/port/device_roster.hpp
                include/ddcs/ctrl/app/device/command_service.hpp
                include/ddcs/ctrl/app/device/policy_service.hpp
                include/ddcs/ctrl/app/device/register_service.hpp
                include/ddcs/ctrl/app/device/status_service.hpp
                include/ddcs/ctrl/app/metrics/port/metrics_source.hpp
                include/ddcs/ctrl/app/metrics/metrics_service.hpp
)
target_link_libraries(
    ddcs_ctrl_app
    PRIVATE ddcs::build_options ddcs::logger
    PUBLIC
        ddcs::cxx_standard
        ddcs::ctrl::domain
        ddcs::wire::acmp
        ddcs::json
        ddcs::device
        ddcs::common
)

add_subdirectory(test/unit)
