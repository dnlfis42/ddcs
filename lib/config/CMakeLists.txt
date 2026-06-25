add_library(ddcs_config STATIC)
add_library(ddcs::config ALIAS ddcs_config)

target_sources(
    ddcs_config
    PRIVATE src/config.cpp
    PUBLIC
        FILE_SET HEADERS BASE_DIRS include FILES include/ddcs/config/config.hpp
)
target_link_libraries(
    ddcs_config
    PRIVATE ddcs::build_options ddcs::common
    PUBLIC ddcs::cxx_standard ddcs::json
)

add_subdirectory(test/unit)
