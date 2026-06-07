add_library(ddcs_common INTERFACE)
add_library(ddcs::common ALIAS ddcs_common)

target_sources(
    ddcs_common
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/common/clock.hpp
                include/ddcs/common/endian.hpp
                include/ddcs/common/fd.hpp
                include/ddcs/common/linear_buffer.hpp
                include/ddcs/common/object_pool.hpp
                include/ddcs/common/ring_buffer.hpp
                include/ddcs/common/strong_value.hpp
                include/ddcs/common/throw_errno.hpp
                include/ddcs/common/uuid.hpp
)
target_link_libraries(ddcs_common INTERFACE compile_option)

# 테스트 경로
add_subdirectory(test/unit)
