add_library(ddcs_net INTERFACE)
add_library(ddcs::net ALIAS ddcs_net)

target_sources(
    ddcs_net
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/net/socket.hpp include/ddcs/net/stream_io.hpp
)
target_link_libraries(ddcs_net INTERFACE ddcs::cxx_standard ddcs::common)

add_subdirectory(test/unit)
