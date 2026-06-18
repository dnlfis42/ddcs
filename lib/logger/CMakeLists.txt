add_library(ddcs_logger STATIC)
add_library(ddcs::logger ALIAS ddcs_logger)

target_sources(
    ddcs_logger
    PRIVATE src/log.cpp
    PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/ddcs/logger/log.hpp
)
target_link_libraries(
    ddcs_logger
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard
)

add_subdirectory(test/unit)
