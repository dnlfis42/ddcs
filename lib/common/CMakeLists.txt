add_library(ddcs_common INTERFACE)
add_library(ddcs::common ALIAS ddcs_common)

target_sources(
    ddcs_common
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/common/circular_buffer.hpp
                include/ddcs/common/clock.hpp
                include/ddcs/common/endian.hpp
                include/ddcs/common/linear_buffer.hpp
                include/ddcs/common/object_pool.hpp
                include/ddcs/common/parse.hpp
                include/ddcs/common/strong_id.hpp
                include/ddcs/common/uuid.hpp
)
target_link_libraries(ddcs_common INTERFACE ddcs::cxx_standard)

add_subdirectory(test/unit)
