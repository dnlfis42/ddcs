add_library(ddcs_io STATIC)
add_library(ddcs::io ALIAS ddcs_io)

target_sources(
    ddcs_io
    PRIVATE src/handler_table.cpp src/reactor.cpp src/timer_queue.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/io/handler_table.hpp
                include/ddcs/io/io_handler.hpp
                include/ddcs/io/reactor.hpp
                include/ddcs/io/timer_handler.hpp
                include/ddcs/io/timer_id.hpp
                include/ddcs/io/timer_queue.hpp
)
target_link_libraries(
    ddcs_io
    PRIVATE compile_option ddcs::logger
    PUBLIC ddcs::common
)

# 테스트 경로
add_subdirectory(test/unit)
