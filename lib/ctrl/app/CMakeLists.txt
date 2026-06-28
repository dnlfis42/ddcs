add_library(ddcs_ctrl_app STATIC)
add_library(ddcs::ctrl::app ALIAS ddcs_ctrl_app)

target_sources(
    ddcs_ctrl_app
    PRIVATE
        src/device/command_service.cpp
        src/device/policy_service.cpp
        src/device/registration_service.cpp
        src/device/status_service.cpp
        src/metrics/metrics_service.cpp
        src/session/command_sender.cpp
        src/session/device_roster.cpp
        src/session/handshake_monitor.cpp
        src/session/liveness_monitor.cpp
        src/session/session_service.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/app/device/port/command_buffer.hpp
                include/ddcs/ctrl/app/device/port/command_id.hpp
                include/ddcs/ctrl/app/device/port/command_sender.hpp
                include/ddcs/ctrl/app/device/port/device_release_sink.hpp
                include/ddcs/ctrl/app/device/port/device_roster.hpp
                include/ddcs/ctrl/app/device/command_service.hpp
                include/ddcs/ctrl/app/device/policy_service.hpp
                include/ddcs/ctrl/app/device/registration_service.hpp
                include/ddcs/ctrl/app/device/status_service.hpp
                include/ddcs/ctrl/app/metrics/port/prometheus_source.hpp
                include/ddcs/ctrl/app/metrics/metrics_service.hpp
                include/ddcs/ctrl/app/session/command_sender.hpp
                include/ddcs/ctrl/app/session/device_roster.hpp
                include/ddcs/ctrl/app/session/handshake_monitor.hpp
                include/ddcs/ctrl/app/session/liveness_monitor.hpp
                include/ddcs/ctrl/app/session/session.hpp
                include/ddcs/ctrl/app/session/session_registry.hpp
                include/ddcs/ctrl/app/session/session_service.hpp
                include/ddcs/ctrl/app/transport/port/connection_id.hpp
                include/ddcs/ctrl/app/transport/port/connection_listener.hpp
                include/ddcs/ctrl/app/transport/port/disconnect_reason.hpp
                include/ddcs/ctrl/app/transport/port/disconnector.hpp
                include/ddcs/ctrl/app/transport/port/message_buffer.hpp
                include/ddcs/ctrl/app/transport/port/message_receiver.hpp
                include/ddcs/ctrl/app/transport/port/message_sender.hpp
)
target_link_libraries(
    ddcs_ctrl_app
    PRIVATE ddcs::build_options ddcs::logger
    PUBLIC
        ddcs::cxx_standard
        ddcs::common
        ddcs::json
        ddcs::device
        ddcs::wire::message
        ddcs::ctrl::domain
)

add_subdirectory(test/unit)
