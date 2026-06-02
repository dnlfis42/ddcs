add_library(ddcs_proto_frame STATIC)
add_library(ddcs::proto::frame ALIAS ddcs_proto_frame)

target_sources(
    ddcs_proto_frame
    PRIVATE src/frame.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/proto/frame/frame.hpp
)
target_link_libraries(ddcs_proto_frame PRIVATE compile_option ddcs::common)

# 테스트 경로
add_subdirectory(test/unit)
