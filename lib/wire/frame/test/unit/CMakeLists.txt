add_executable(ddcs_wire_frame_frame_test ddcs_wire_frame_frame_test.cpp)
target_link_libraries(
    ddcs_wire_frame_frame_test
    PRIVATE compile_option ddcs::wire::frame GTest::gtest_main
)
add_test(NAME ddcs_wire_frame_frame_test COMMAND ddcs_wire_frame_frame_test)
