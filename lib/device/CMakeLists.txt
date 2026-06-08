add_library(ddcs_device INTERFACE)
add_library(ddcs::device ALIAS ddcs_device)

target_sources(
    ddcs_device
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/device/mode.hpp include/ddcs/device/status.hpp
)
