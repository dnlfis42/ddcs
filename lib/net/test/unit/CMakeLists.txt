ddcs_add_unit_test(
    ddcs_net_stream_io_test
    SOURCES ddcs_net_stream_io_test.cpp
    LIBRARIES ddcs::common ddcs::io ddcs::net
)

ddcs_add_unit_test(
    ddcs_net_socket_test
    SOURCES ddcs_net_socket_test.cpp
    LIBRARIES ddcs::common ddcs::io ddcs::net
)
