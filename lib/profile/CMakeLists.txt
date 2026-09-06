add_library(ddcs_profile STATIC)
add_library(ddcs::profile ALIAS ddcs_profile)

target_sources(
    ddcs_profile
    PRIVATE
        src/analysis.cpp
        src/dump.cpp
        src/profile.cpp
        src/recorder.cpp
        src/serializer.cpp
        src/tick_metrics.cpp
        src/timestamp_converter.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/profile/analysis.hpp
                include/ddcs/profile/recording_metadata.hpp
                include/ddcs/profile/recorder.hpp
                include/ddcs/profile/dump.hpp
                include/ddcs/profile/serializer.hpp
                include/ddcs/profile/tick_sample.hpp
                include/ddcs/profile/tick_metrics.hpp
                include/ddcs/profile/timestamp_converter.hpp
)
target_link_libraries(
    ddcs_profile
    PRIVATE ddcs::build_options ddcs::json
    PUBLIC ddcs::common ddcs::cxx_standard
)

add_subdirectory(test/unit)
