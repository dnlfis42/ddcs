add_library(ddcs_net_frame STATIC)
add_library(ddcs::net::frame ALIAS ddcs_net_frame)

target_sources(
    ddcs_net_frame
    PRIVATE src/frame.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/net/frame/frame.hpp
)
target_link_libraries(ddcs_net_frame PRIVATE compile_option)

# 테스트 경로
add_subdirectory(test/unit)
