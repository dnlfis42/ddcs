add_executable(ddcs_logger_log_test ddcs_logger_log_test.cpp)
target_link_libraries(
    ddcs_logger_log_test
    PRIVATE compile_option ddcs::logger ddcs::json GTest::gtest_main
)
add_test(NAME ddcs_logger_log_test COMMAND ddcs_logger_log_test)
