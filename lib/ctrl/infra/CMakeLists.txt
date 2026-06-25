add_library(ddcs_ctrl_infra STATIC)
add_library(ddcs::ctrl::infra ALIAS ddcs_ctrl_infra)

target_sources(
    ddcs_ctrl_infra
    PRIVATE
        src/prometheus/connection.cpp
        src/prometheus/server.cpp
        src/transport/acceptor.cpp
        src/transport/connection.cpp
        src/transport/peer_address.cpp
        src/transport/server.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/infra/prometheus/connection.hpp
                include/ddcs/ctrl/infra/prometheus/server.hpp
                include/ddcs/ctrl/infra/transport/acceptor.hpp
                include/ddcs/ctrl/infra/transport/connection.hpp
                include/ddcs/ctrl/infra/transport/peer_address.hpp
                include/ddcs/ctrl/infra/transport/server.hpp
)
target_link_libraries(
    ddcs_ctrl_infra
    PRIVATE ddcs::build_options ddcs::logger ddcs::wire::frame
    PUBLIC ddcs::cxx_standard ddcs::common ddcs::io ddcs::net ddcs::ctrl::app
)

add_subdirectory(test/unit)
