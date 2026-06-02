add_executable(ddcs_json_value_test ddcs_json_value_test.cpp)
target_link_libraries(
    ddcs_json_value_test
    PRIVATE compile_option ddcs::json GTest::gtest_main
)
add_test(NAME ddcs_json_value_test COMMAND ddcs_json_value_test)
