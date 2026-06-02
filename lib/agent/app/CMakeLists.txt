add_library(ddcs_agent_app STATIC)
add_library(ddcs::agent::app ALIAS ddcs_agent_app)

target_sources(
    ddcs_agent_app
    PRIVATE src/session_service.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/agent/app/session_service.hpp
)
target_link_libraries(
    ddcs_agent_app
    PRIVATE compile_option ddcs::logger ddcs::proto::cmd ddcs::json
    PUBLIC ddcs::common ddcs::agent::domain ddcs::agent::port ddcs::proto::msg
)

add_subdirectory(test/unit)
