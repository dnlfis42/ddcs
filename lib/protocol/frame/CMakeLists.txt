add_library(ddcs_protocol_frame STATIC)
add_library(ddcs::protocol::frame ALIAS ddcs_protocol_frame)

target_sources(
    ddcs_protocol_frame
    PRIVATE src/frame.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/protocol/frame/frame.hpp
)
target_link_libraries(ddcs_protocol_frame PRIVATE compile_option)

# 테스트 경로
add_subdirectory(test/unit)
