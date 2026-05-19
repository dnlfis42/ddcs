add_library(ddcs_net_message INTERFACE)
add_library(ddcs::net::message ALIAS ddcs_net_message)

target_sources(
    ddcs_net_message
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/net/message/message.hpp
)
target_link_libraries(ddcs_net_message INTERFACE compile_option ddcs::common)
