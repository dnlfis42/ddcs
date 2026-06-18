add_library(ddcs_ctrl_infra STATIC)
add_library(ddcs::ctrl::infra ALIAS ddcs_ctrl_infra)

target_sources(
    ddcs_ctrl_infra
    PRIVATE
        src/frame/acceptor.cpp
        src/frame/connection.cpp
        src/frame/peer_address.cpp
        src/frame/server.cpp
        src/prometheus/connection.cpp
        src/prometheus/server.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/infra/frame/acceptor.hpp
                include/ddcs/ctrl/infra/frame/connection.hpp
                include/ddcs/ctrl/infra/frame/peer_address.hpp
                include/ddcs/ctrl/infra/frame/server.hpp
                include/ddcs/ctrl/infra/prometheus/connection.hpp
                include/ddcs/ctrl/infra/prometheus/server.hpp
)
target_link_libraries(
    ddcs_ctrl_infra
    PRIVATE ddcs::build_options ddcs::wire::frame ddcs::logger
    PUBLIC ddcs::cxx_standard ddcs::ctrl::app ddcs::io ddcs::common
)

add_subdirectory(test/unit)
