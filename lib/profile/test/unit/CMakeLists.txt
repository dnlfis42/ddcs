ddcs_add_unit_test(
    ddcs_profile_analysis_test
    SOURCES ddcs_profile_analysis_test.cpp
    LIBRARIES ddcs::profile
)

ddcs_add_unit_test(
    ddcs_profile_recorder_test
    SOURCES ddcs_profile_recorder_test.cpp
    LIBRARIES ddcs::profile
)

ddcs_add_unit_test(
    ddcs_profile_timestamp_converter_test
    SOURCES ddcs_profile_timestamp_converter_test.cpp
    LIBRARIES ddcs::profile
)

ddcs_add_unit_test(
    ddcs_profile_serializer_test
    SOURCES ddcs_profile_serializer_test.cpp
    LIBRARIES ddcs::profile ddcs::json
)

ddcs_add_unit_test(
    ddcs_profile_dump_test
    SOURCES ddcs_profile_dump_test.cpp
    LIBRARIES ddcs::profile ddcs::json
)

ddcs_add_unit_test(
    ddcs_profile_tick_metrics_test
    SOURCES ddcs_profile_tick_metrics_test.cpp
    LIBRARIES ddcs::profile
)
