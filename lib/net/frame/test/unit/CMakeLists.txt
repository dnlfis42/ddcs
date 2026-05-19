add_executable(ddcs_net_frame_frame_test ddcs_net_frame_frame_test.cpp)
target_link_libraries(
    ddcs_net_frame_frame_test
    PRIVATE compile_option ddcs::net::frame GTest::gtest_main
)
add_test(NAME ddcs_net_frame_frame_test COMMAND ddcs_net_frame_frame_test)
