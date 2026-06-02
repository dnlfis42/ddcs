add_executable(ddcs_io_handler_table_test ddcs_io_handler_table_test.cpp)
target_link_libraries(
    ddcs_io_handler_table_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_handler_table_test COMMAND ddcs_io_handler_table_test)

add_executable(ddcs_io_timer_queue_test ddcs_io_timer_queue_test.cpp)
target_link_libraries(
    ddcs_io_timer_queue_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_timer_queue_test COMMAND ddcs_io_timer_queue_test)

add_executable(ddcs_io_reactor_test ddcs_io_reactor_test.cpp)
target_link_libraries(
    ddcs_io_reactor_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_reactor_test COMMAND ddcs_io_reactor_test)
