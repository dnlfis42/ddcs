add_library(ddcs_json STATIC)
add_library(ddcs::json ALIAS ddcs_json)

target_sources(
    ddcs_json
    PRIVATE src/value.cpp
    PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/ddcs/json/value.hpp
)
target_link_libraries(
    ddcs_json
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard
)

add_subdirectory(test/unit)
