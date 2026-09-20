# 라이브러리 경로
add_subdirectory(domain)
add_subdirectory(app)
add_subdirectory(infra)

# 공개 facade 타깃
add_library(ddcs_agent STATIC)
add_library(ddcs::agent ALIAS ddcs_agent)

target_sources(
    ddcs_agent
    PRIVATE src/agent.cpp src/agent_fleet.cpp src/bootstrap.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/agent/agent.hpp
                include/ddcs/agent/agent_fleet.hpp
                include/ddcs/agent/bootstrap.hpp
)
target_link_libraries(
    ddcs_agent
    PRIVATE
        ddcs::build_options
        ddcs::config
        ddcs::io
        ddcs::wire::frame
        ddcs::agent::infra
    PUBLIC
        ddcs::cxx_standard
        ddcs::common
        ddcs::logger
        ddcs::agent::domain
        ddcs::agent::app
)

add_subdirectory(test/unit)
