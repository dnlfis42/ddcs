ddcs_add_unit_test(
    ddcs_io_channel_events_test
    SOURCES ddcs_io_channel_events_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_channel_registry_test
    SOURCES ddcs_io_channel_registry_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_channel_test
    SOURCES ddcs_io_channel_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_fd_test
    SOURCES ddcs_io_fd_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_throw_errno_test
    SOURCES ddcs_io_throw_errno_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_reactor_test
    SOURCES ddcs_io_reactor_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_signal_source_test
    SOURCES ddcs_io_signal_source_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_timer_queue_test
    SOURCES ddcs_io_timer_queue_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_timer_registration_table_test
    SOURCES ddcs_io_timer_registration_table_test.cpp
    LIBRARIES ddcs::io
)

ddcs_add_unit_test(
    ddcs_io_timer_scheduler_test
    SOURCES ddcs_io_timer_scheduler_test.cpp
    LIBRARIES ddcs::io
)
