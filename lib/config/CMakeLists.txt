add_library(ddcs_config STATIC)
add_library(ddcs::config ALIAS ddcs_config)

target_sources(
    ddcs_config
    PRIVATE src/file.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/config/env.hpp
                include/ddcs/config/file.hpp
)
target_link_libraries(
    ddcs_config
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard ddcs::common ddcs::json ddcs::logger
)

add_subdirectory(test/unit)
