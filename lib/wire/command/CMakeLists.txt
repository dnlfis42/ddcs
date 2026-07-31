add_library(ddcs_wire_command STATIC)
add_library(ddcs::wire::command ALIAS ddcs_wire_command)

target_sources(
    ddcs_wire_command
    PRIVATE src/command.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/wire/command/command.hpp
)
target_link_libraries(
    ddcs_wire_command
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard
)

add_subdirectory(test/unit)
