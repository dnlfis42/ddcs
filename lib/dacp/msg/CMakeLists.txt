add_library(ddcs_dacp_msg STATIC)
add_library(ddcs::dacp::msg ALIAS ddcs_dacp_msg)

target_sources(
    ddcs_dacp_msg
    PRIVATE src/message.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/dacp/msg/message.hpp
                include/ddcs/dacp/msg/type.hpp
)
target_link_libraries(ddcs_dacp_msg PRIVATE compile_option PUBLIC ddcs::common)

add_subdirectory(test/unit)
