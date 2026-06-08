# 라이브러리 경로
add_subdirectory(domain)
add_subdirectory(port)
add_subdirectory(app)
add_subdirectory(infra)

# 공개 facade 타깃
add_library(ddcs_agent STATIC)
add_library(ddcs::agent ALIAS ddcs_agent)
target_sources(
    ddcs_agent
    PRIVATE src/agent.cpp
    PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/ddcs/agent/agent.hpp
)
target_link_libraries(
    ddcs_agent
    PRIVATE compile_option
    PUBLIC
        ddcs::common
        ddcs::logger
        ddcs::runtime
        ddcs::agent::domain
        ddcs::agent::app
        ddcs::agent::infra
)

add_subdirectory(test/unit)
