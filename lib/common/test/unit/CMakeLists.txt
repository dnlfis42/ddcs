ddcs_add_unit_test(
    ddcs_common_clock_test
    SOURCES ddcs_common_clock_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_endian_test
    SOURCES ddcs_common_endian_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_fd_test
    SOURCES ddcs_common_fd_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_linear_buffer_test
    SOURCES ddcs_common_linear_buffer_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_object_pool_test
    SOURCES ddcs_common_object_pool_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_ring_buffer_test
    SOURCES ddcs_common_ring_buffer_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_strong_value_test
    SOURCES ddcs_common_strong_value_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_throw_errno_test
    SOURCES ddcs_common_throw_errno_test.cpp
    LIBRARIES ddcs::common
)

ddcs_add_unit_test(
    ddcs_common_uuid_test
    SOURCES ddcs_common_uuid_test.cpp
    LIBRARIES ddcs::common
)
