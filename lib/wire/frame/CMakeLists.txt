add_library(ddcs_wire_frame STATIC)
add_library(ddcs::wire::frame ALIAS ddcs_wire_frame)

target_sources(
    ddcs_wire_frame
    PRIVATE src/extract.cpp src/frame.cpp src/seal.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/wire/frame/extract.hpp
                include/ddcs/wire/frame/frame.hpp
                include/ddcs/wire/frame/seal.hpp
)
target_link_libraries(
    ddcs_wire_frame
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard ddcs::common
)

add_subdirectory(test/unit)
