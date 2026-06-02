add_executable(ddcs_proto_frame_frame_test ddcs_proto_frame_frame_test.cpp)
target_link_libraries(
    ddcs_proto_frame_frame_test
    PRIVATE compile_option ddcs::proto::frame GTest::gtest_main
)
add_test(NAME ddcs_proto_frame_frame_test COMMAND ddcs_proto_frame_frame_test)
