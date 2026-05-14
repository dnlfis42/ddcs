add_library(ddcs_core INTERFACE)
add_library(ddcs::core ALIAS ddcs_core)

target_sources(
    ddcs_core
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/core/ring_buffer.hpp
)
target_link_libraries(ddcs_core INTERFACE compile_option)

# 테스트 경로
add_subdirectory(test)
