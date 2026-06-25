ddcs_add_unit_test(
    ddcs_wire_message_command_test
    SOURCES ddcs_wire_message_command_test.cpp
    LIBRARIES ddcs::wire::message
)

ddcs_add_unit_test(
    ddcs_wire_message_message_test
    SOURCES ddcs_wire_message_message_test.cpp
    LIBRARIES ddcs::wire::message
)
