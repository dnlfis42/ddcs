add_library(ddcs_proto_cmd STATIC)
add_library(ddcs::proto::cmd ALIAS ddcs_proto_cmd)

target_sources(
    ddcs_proto_cmd
    PRIVATE src/command.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/proto/cmd/command.hpp
)
target_link_libraries(
    ddcs_proto_cmd
    PRIVATE compile_option
    PUBLIC ddcs::common ddcs::device
)

add_subdirectory(test/unit)
