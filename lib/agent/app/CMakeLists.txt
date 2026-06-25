add_library(ddcs_agent_app STATIC)
add_library(ddcs::agent::app ALIAS ddcs_agent_app)

target_sources(
    ddcs_agent_app
    PRIVATE src/session/session_service.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/agent/app/session/session_service.hpp
                include/ddcs/agent/app/transport/port/inbound.hpp
                include/ddcs/agent/app/transport/port/message_buffer.hpp
                include/ddcs/agent/app/transport/port/outbound.hpp
                include/ddcs/agent/app/transport/port/timer_slot.hpp
)
target_link_libraries(
    ddcs_agent_app
    PRIVATE ddcs::build_options ddcs::json ddcs::logger ddcs::device
    PUBLIC
        ddcs::cxx_standard
        ddcs::common
        ddcs::wire::message
        ddcs::agent::domain
)

add_subdirectory(test/unit)
