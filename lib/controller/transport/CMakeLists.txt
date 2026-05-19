add_library(ddcs_controller_transport INTERFACE)
add_library(ddcs::controller::transport ALIAS ddcs_controller_transport)

target_sources(
    ddcs_controller_transport
    INTERFACE FILE_SET HEADERS BASE_DIRS include
)

target_link_libraries(ddcs_controller_transport INTERFACE compile_option)
