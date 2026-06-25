add_library(ddcs_wire_message STATIC)
add_library(ddcs::wire::message ALIAS ddcs_wire_message)

target_sources(
    ddcs_wire_message
    PRIVATE src/command.cpp src/message.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/wire/message/command.hpp
                include/ddcs/wire/message/message.hpp
)
target_link_libraries(
    ddcs_wire_message
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard ddcs::common
)

add_subdirectory(test/unit)
