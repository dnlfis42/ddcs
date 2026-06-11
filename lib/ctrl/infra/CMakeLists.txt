add_library(ddcs_ctrl_infra STATIC)
add_library(ddcs::ctrl::infra ALIAS ddcs_ctrl_infra)

target_sources(
    ddcs_ctrl_infra
    PRIVATE
        src/dacp/acceptor.cpp
        src/dacp/connection.cpp
        src/dacp/peer_address.cpp
        src/dacp/server.cpp
        src/metrics/connection.cpp
        src/metrics/server.cpp
        src/transport/connection.cpp
        src/transport/connection_acceptor.cpp
        src/transport/connection_manager.cpp
        src/transport/endpoint.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/infra/dacp/acceptor.hpp
                include/ddcs/ctrl/infra/dacp/connection.hpp
                include/ddcs/ctrl/infra/dacp/peer_address.hpp
                include/ddcs/ctrl/infra/dacp/server.hpp
                include/ddcs/ctrl/infra/metrics/connection.hpp
                include/ddcs/ctrl/infra/metrics/server.hpp
                include/ddcs/ctrl/infra/transport/connection.hpp
                include/ddcs/ctrl/infra/transport/connection_acceptor.hpp
                include/ddcs/ctrl/infra/transport/connection_manager.hpp
                include/ddcs/ctrl/infra/transport/endpoint.hpp
)
target_link_libraries(
    ddcs_ctrl_infra
    PRIVATE compile_option ddcs::logger ddcs::proto::frame ddcs::dacp::frame
    PUBLIC ddcs::common ddcs::ctrl::app ddcs::ctrl::port ddcs::io ddcs::runtime
)

add_subdirectory(test/unit)
