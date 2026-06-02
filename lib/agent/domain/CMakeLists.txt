add_library(ddcs_agent_domain STATIC)
add_library(ddcs::agent::domain ALIAS ddcs_agent_domain)

target_sources(
    ddcs_agent_domain
    PRIVATE src/dummy_device.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/agent/domain/device.hpp
                include/ddcs/agent/domain/dummy_device.hpp
)
target_link_libraries(
    ddcs_agent_domain
    PRIVATE compile_option
    PUBLIC ddcs::proto::cmd ddcs::device
)

add_subdirectory(test/unit)
