add_library(ddcs_dacp_frame STATIC)
add_library(ddcs::dacp::frame ALIAS ddcs_dacp_frame)

target_sources(
    ddcs_dacp_frame
    PRIVATE src/frame.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/dacp/frame/frame.hpp
)
target_link_libraries(ddcs_dacp_frame PRIVATE compile_option ddcs::common)

add_subdirectory(test/unit)
