add_executable(
    ddcs_runtime_fd_dispatch_table_test
    ddcs_runtime_fd_dispatch_table_test.cpp
)
target_link_libraries(
    ddcs_runtime_fd_dispatch_table_test
    PRIVATE compile_option ddcs::runtime GTest::gtest_main
)
add_test(
    NAME ddcs_runtime_fd_dispatch_table_test
    COMMAND ddcs_runtime_fd_dispatch_table_test
)

add_executable(ddcs_runtime_timer_queue_test ddcs_runtime_timer_queue_test.cpp)
target_link_libraries(
    ddcs_runtime_timer_queue_test
    PRIVATE compile_option ddcs::runtime GTest::gtest_main
)
add_test(
    NAME ddcs_runtime_timer_queue_test
    COMMAND ddcs_runtime_timer_queue_test
)

add_executable(ddcs_runtime_reactor_test ddcs_runtime_reactor_test.cpp)
target_link_libraries(
    ddcs_runtime_reactor_test
    PRIVATE compile_option ddcs::runtime GTest::gtest_main
)
add_test(NAME ddcs_runtime_reactor_test COMMAND ddcs_runtime_reactor_test)
