add_executable(ddcs_core_ring_buffer_test ddcs_core_ring_buffer_test.cpp)
target_link_libraries(
    ddcs_core_ring_buffer_test
    PRIVATE compile_option ddcs::core GTest::gtest_main
)
add_test(NAME ddcs_core_ring_buffer_test COMMAND ddcs_core_ring_buffer_test)
