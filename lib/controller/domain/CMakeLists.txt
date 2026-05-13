add_library(ddcs_controller_domain INTERFACE)
add_library(ddcs::controller::domain ALIAS ddcs_controller_domain)

target_sources(
    ddcs_controller_domain
    INTERFACE FILE_SET HEADERS BASE_DIRS include
)

target_link_libraries(ddcs_controller_domain INTERFACE compile_option)
