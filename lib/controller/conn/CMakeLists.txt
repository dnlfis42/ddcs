add_library(ddcs_controller_conn INTERFACE)
add_library(ddcs::controller::conn ALIAS ddcs_controller_conn)

target_sources(
    ddcs_controller_conn
    INTERFACE FILE_SET HEADERS BASE_DIRS include
)

target_link_libraries(ddcs_controller_conn INTERFACE compile_option)
