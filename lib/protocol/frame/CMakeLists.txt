add_library(ddcs_protocol_frame INTERFACE)
add_library(ddcs::protocol::frame ALIAS ddcs_protocol_frame)

target_sources(ddcs_protocol_frame INTERFACE FILE_SET HEADERS BASE_DIRS include)

target_link_libraries(ddcs_protocol_frame INTERFACE compile_option)
