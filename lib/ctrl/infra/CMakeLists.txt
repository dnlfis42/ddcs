add_library(ddcs_ctrl_infra STATIC)
add_library(ddcs::ctrl::infra ALIAS ddcs_ctrl_infra)

# 5a Endpoint, 5b Connection, 5c ConnectionManager, 5d ConnectionAcceptor.
target_sources(
    ddcs_ctrl_infra
    PRIVATE
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
                include/ddcs/ctrl/infra/metrics/connection.hpp
                include/ddcs/ctrl/infra/metrics/server.hpp
                include/ddcs/ctrl/infra/transport/connection.hpp
                include/ddcs/ctrl/infra/transport/connection_acceptor.hpp
                include/ddcs/ctrl/infra/transport/connection_manager.hpp
                include/ddcs/ctrl/infra/transport/endpoint.hpp
)
target_link_libraries(
    ddcs_ctrl_infra
    PRIVATE compile_option ddcs::logger ddcs::proto::frame
    PUBLIC ddcs::common ddcs::ctrl::port ddcs::runtime
)

# 테스트 경로
add_subdirectory(test/unit)
