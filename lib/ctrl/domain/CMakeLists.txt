add_library(ddcs_ctrl_domain STATIC)
add_library(ddcs::ctrl::domain ALIAS ddcs_ctrl_domain)

target_sources(
    ddcs_ctrl_domain
    PRIVATE src/device_registry.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/domain/device_id.hpp
                include/ddcs/ctrl/domain/device_registry.hpp
                include/ddcs/ctrl/domain/device_shadow.hpp
                include/ddcs/ctrl/domain/group_policy.hpp
                include/ddcs/ctrl/domain/status.hpp
)
target_link_libraries(
    ddcs_ctrl_domain
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard ddcs::common ddcs::device
)

add_subdirectory(test/unit)
