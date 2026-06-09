add_executable(ddcs_dacp_frame_frame_test ddcs_dacp_frame_frame_test.cpp)
target_link_libraries(
    ddcs_dacp_frame_frame_test
    PRIVATE compile_option ddcs::dacp::frame GTest::gtest_main
)
add_test(NAME ddcs_dacp_frame_frame_test COMMAND ddcs_dacp_frame_frame_test)
