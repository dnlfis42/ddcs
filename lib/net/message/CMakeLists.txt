add_library(ddcs_net_message STATIC)
add_library(ddcs::net::message ALIAS ddcs_net_message)

target_sources(
    ddcs_net_message
    PRIVATE src/message.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/net/message/message.hpp
)
target_link_libraries(ddcs_net_message PRIVATE compile_option)

# 테스트 경로
add_subdirectory(test/unit)
