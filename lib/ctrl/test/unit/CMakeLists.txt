ddcs_add_unit_test(
    ddcs_ctrl_controller_test
    SOURCES ddcs_ctrl_controller_test.cpp
    LIBRARIES ddcs::ctrl ddcs::profile
)

ddcs_add_unit_test(
    ddcs_ctrl_tick_execution_test
    SOURCES ddcs_ctrl_tick_execution_test.cpp
    LIBRARIES ddcs::ctrl ddcs::profile
)
