add_executable(ddcs_common_clock_test ddcs_common_clock_test.cpp)
target_link_libraries(
    ddcs_common_clock_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_common_clock_test COMMAND ddcs_common_clock_test)

add_executable(ddcs_common_endian_test ddcs_common_endian_test.cpp)
target_link_libraries(
    ddcs_common_endian_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_common_endian_test COMMAND ddcs_common_endian_test)

add_executable(ddcs_common_fd_test ddcs_common_fd_test.cpp)
target_link_libraries(
    ddcs_common_fd_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_common_fd_test COMMAND ddcs_common_fd_test)

add_executable(
    ddcs_common_linear_buffer_test
    ddcs_common_linear_buffer_test.cpp
)
target_link_libraries(
    ddcs_common_linear_buffer_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(
    NAME ddcs_common_linear_buffer_test
    COMMAND ddcs_common_linear_buffer_test
)

add_executable(ddcs_common_object_pool_test ddcs_common_object_pool_test.cpp)
target_link_libraries(
    ddcs_common_object_pool_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_common_object_pool_test COMMAND ddcs_common_object_pool_test)

add_executable(ddcs_common_ring_buffer_test ddcs_common_ring_buffer_test.cpp)
target_link_libraries(
    ddcs_common_ring_buffer_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_common_ring_buffer_test COMMAND ddcs_common_ring_buffer_test)

add_executable(ddcs_common_strong_value_test ddcs_common_strong_value_test.cpp)
target_link_libraries(
    ddcs_common_strong_value_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(
    NAME ddcs_common_strong_value_test
    COMMAND ddcs_common_strong_value_test
)

add_executable(ddcs_common_throw_errno_test ddcs_common_throw_errno_test.cpp)
target_link_libraries(
    ddcs_common_throw_errno_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_common_throw_errno_test COMMAND ddcs_common_throw_errno_test)

add_executable(ddcs_common_uuid_test ddcs_common_uuid_test.cpp)
target_link_libraries(
    ddcs_common_uuid_test
    PRIVATE compile_option ddcs::common GTest::gtest_main
)
add_test(NAME ddcs_common_uuid_test COMMAND ddcs_common_uuid_test)
