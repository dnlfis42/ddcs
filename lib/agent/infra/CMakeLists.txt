add_library(ddcs_agent_infra STATIC)
add_library(ddcs::agent::infra ALIAS ddcs_agent_infra)

target_sources(
    ddcs_agent_infra
    PRIVATE src/backoff_schedule.cpp src/connection.cpp src/connector.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/agent/infra/backoff_schedule.hpp
                include/ddcs/agent/infra/connection.hpp
                include/ddcs/agent/infra/connector.hpp
)
target_link_libraries(
    ddcs_agent_infra
    PRIVATE compile_option ddcs::logger ddcs::proto::frame
    PUBLIC ddcs::common ddcs::runtime ddcs::agent::port
)

add_subdirectory(test/unit)
