add_library(ddcs_device INTERFACE)
add_library(ddcs::device ALIAS ddcs_device)

target_sources(
    ddcs_device
    INTERFACE
        FILE_SET HEADERS BASE_DIRS include FILES include/ddcs/device/mode.hpp
)
target_link_libraries(ddcs_device INTERFACE ddcs::cxx_standard ddcs::common)

add_subdirectory(test/unit)
