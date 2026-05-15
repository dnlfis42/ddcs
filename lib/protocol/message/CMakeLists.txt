add_library(ddcs_protocol_message STATIC)
add_library(ddcs::protocol::message ALIAS ddcs_protocol_message)

target_sources(
    ddcs_protocol_message
    PRIVATE src/message.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/protocol/message/message.hpp
)
target_link_libraries(ddcs_protocol_message PRIVATE compile_option)

# 테스트 경로
add_subdirectory(test/unit)
