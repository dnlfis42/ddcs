add_library(ddcs_device STATIC)
add_library(ddcs::device ALIAS ddcs_device)

target_sources(
    ddcs_device
    PRIVATE src/command.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/device/command.hpp
                include/ddcs/device/mode.hpp
                include/ddcs/device/status.hpp
)
target_link_libraries(
    ddcs_device
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard ddcs::common
)

add_subdirectory(test/unit)
