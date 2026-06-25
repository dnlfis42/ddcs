ddcs_add_unit_test(
    ddcs_wire_frame_extract_test
    SOURCES ddcs_wire_frame_extract_test.cpp
    LIBRARIES ddcs::common ddcs::wire::frame
)

ddcs_add_unit_test(
    ddcs_wire_frame_frame_test
    SOURCES ddcs_wire_frame_frame_test.cpp
    LIBRARIES ddcs::wire::frame
)
