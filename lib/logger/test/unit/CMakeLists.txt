ddcs_add_unit_test(
    ddcs_logger_log_test
    SOURCES ddcs_logger_log_test.cpp
    LIBRARIES ddcs::logger ddcs::json
)
