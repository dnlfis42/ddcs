add_executable(ddcs_net_codec_endian_test ddcs_net_codec_endian_test.cpp)
target_link_libraries(
    ddcs_net_codec_endian_test
    PRIVATE compile_option ddcs::net::codec GTest::gtest_main
)
add_test(NAME ddcs_net_codec_endian_test COMMAND ddcs_net_codec_endian_test)
