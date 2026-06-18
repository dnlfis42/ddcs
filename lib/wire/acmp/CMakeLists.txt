add_library(ddcs_wire_acmp STATIC)
add_library(ddcs::wire::acmp ALIAS ddcs_wire_acmp)

target_sources(
    ddcs_wire_acmp
    PRIVATE src/message.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/wire/acmp/message.hpp
)
target_link_libraries(
    ddcs_wire_acmp
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard ddcs::common
)

add_subdirectory(test/unit)
