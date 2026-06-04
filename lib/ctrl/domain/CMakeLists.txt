add_library(ddcs_ctrl_domain STATIC)
add_library(ddcs::ctrl::domain ALIAS ddcs_ctrl_domain)

target_sources(
    ddcs_ctrl_domain
    PRIVATE src/device_registry.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/ctrl/domain/device.hpp
                include/ddcs/ctrl/domain/device_id.hpp
                include/ddcs/ctrl/domain/device_registry.hpp
                include/ddcs/ctrl/domain/policy.hpp
)
target_link_libraries(
    ddcs_ctrl_domain
    PRIVATE compile_option
    PUBLIC ddcs::common ddcs::device
)

# 테스트 경로
add_subdirectory(test/unit)
