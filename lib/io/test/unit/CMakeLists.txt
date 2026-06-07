add_executable(ddcs_io_channel_events_test ddcs_io_channel_events_test.cpp)
target_link_libraries(
    ddcs_io_channel_events_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_channel_events_test COMMAND ddcs_io_channel_events_test)

add_executable(ddcs_io_channel_registry_test ddcs_io_channel_registry_test.cpp)
target_link_libraries(
    ddcs_io_channel_registry_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(
    NAME ddcs_io_channel_registry_test
    COMMAND ddcs_io_channel_registry_test
)

add_executable(ddcs_io_channel_test ddcs_io_channel_test.cpp)
target_link_libraries(
    ddcs_io_channel_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_channel_test COMMAND ddcs_io_channel_test)

add_executable(ddcs_io_reactor_test ddcs_io_reactor_test.cpp)
target_link_libraries(
    ddcs_io_reactor_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_reactor_test COMMAND ddcs_io_reactor_test)

add_executable(ddcs_io_signal_source_test ddcs_io_signal_source_test.cpp)
target_link_libraries(
    ddcs_io_signal_source_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_signal_source_test COMMAND ddcs_io_signal_source_test)

add_executable(ddcs_io_timer_queue_test ddcs_io_timer_queue_test.cpp)
target_link_libraries(
    ddcs_io_timer_queue_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_timer_queue_test COMMAND ddcs_io_timer_queue_test)

add_executable(
    ddcs_io_timer_registration_table_test
    ddcs_io_timer_registration_table_test.cpp
)
target_link_libraries(
    ddcs_io_timer_registration_table_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(
    NAME ddcs_io_timer_registration_table_test
    COMMAND ddcs_io_timer_registration_table_test
)

add_executable(ddcs_io_timer_scheduler_test ddcs_io_timer_scheduler_test.cpp)
target_link_libraries(
    ddcs_io_timer_scheduler_test
    PRIVATE compile_option ddcs::io GTest::gtest_main
)
add_test(NAME ddcs_io_timer_scheduler_test COMMAND ddcs_io_timer_scheduler_test)
