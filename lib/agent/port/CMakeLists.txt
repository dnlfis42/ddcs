add_library(ddcs_agent_port INTERFACE)
add_library(ddcs::agent::port ALIAS ddcs_agent_port)

target_sources(
    ddcs_agent_port
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/agent/port/timer_id.hpp
                include/ddcs/agent/port/inbound.hpp
                include/ddcs/agent/port/outbound.hpp
)
target_link_libraries(ddcs_agent_port INTERFACE compile_option ddcs::common)

add_subdirectory(test/unit)
