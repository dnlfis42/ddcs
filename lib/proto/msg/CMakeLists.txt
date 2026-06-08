add_library(ddcs_proto_msg STATIC)
add_library(ddcs::proto::msg ALIAS ddcs_proto_msg)

target_sources(
    ddcs_proto_msg
    PRIVATE src/message.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/proto/msg/message.hpp
                include/ddcs/proto/msg/type.hpp
)
target_link_libraries(ddcs_proto_msg PRIVATE compile_option PUBLIC ddcs::common)

add_subdirectory(test/unit)
