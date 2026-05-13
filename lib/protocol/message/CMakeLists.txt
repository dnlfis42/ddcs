add_library(ddcs_protocol_message INTERFACE)
add_library(ddcs::protocol::message ALIAS ddcs_protocol_message)

target_sources(
    ddcs_protocol_message
    INTERFACE FILE_SET HEADERS BASE_DIRS include
)

target_link_libraries(
    ddcs_protocol_message
    INTERFACE compile_option ddcs::protocol::frame
)
