add_library(ddcs_agent_infra STATIC)
add_library(ddcs::agent::infra ALIAS ddcs_agent_infra)

target_sources(
    ddcs_agent_infra
    PRIVATE
        src/backoff_schedule.cpp
        src/frame/connection.cpp
        src/frame/connector.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/agent/infra/backoff_schedule.hpp
                include/ddcs/agent/infra/frame/connection.hpp
                include/ddcs/agent/infra/frame/connector.hpp
)
target_link_libraries(
    ddcs_agent_infra
    PRIVATE ddcs::build_options ddcs::wire::frame ddcs::logger
    PUBLIC ddcs::cxx_standard ddcs::agent::app ddcs::io ddcs::common
)

add_subdirectory(test/unit)
