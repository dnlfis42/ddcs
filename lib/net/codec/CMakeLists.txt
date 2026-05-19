add_library(ddcs_net_codec INTERFACE)
add_library(ddcs::net::codec ALIAS ddcs_net_codec)

target_sources(
    ddcs_net_codec
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/net/codec/endian.hpp
)
target_link_libraries(ddcs_net_codec INTERFACE compile_option)

# 테스트 경로
add_subdirectory(test/unit)
