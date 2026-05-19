add_library(ddcs_controller_session INTERFACE)
add_library(ddcs::controller::session ALIAS ddcs_controller_session)

target_sources(
    ddcs_controller_session
    INTERFACE FILE_SET HEADERS BASE_DIRS include
)
target_link_libraries(
    ddcs_controller_session
    INTERFACE
        compile_option
        ddcs::controller::transport
        ddcs::controller::domain
        ddcs::net::frame
        ddcs::net::message
)
