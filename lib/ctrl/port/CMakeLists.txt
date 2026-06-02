add_library(ddcs_ctrl_port INTERFACE)
add_library(ddcs::ctrl::port ALIAS ddcs_ctrl_port)

target_sources(
    ddcs_ctrl_port
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/port/metrics/inbound.hpp
                include/ddcs/ctrl/port/transport/connection_id.hpp
                include/ddcs/ctrl/port/transport/inbound.hpp
                include/ddcs/ctrl/port/transport/outbound.hpp
)
target_link_libraries(ddcs_ctrl_port INTERFACE compile_option ddcs::common)

# 테스트 경로
add_subdirectory(test/unit)
