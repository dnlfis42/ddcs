add_library(ddcs_runtime STATIC)
add_library(ddcs::runtime ALIAS ddcs_runtime)

target_sources(
    ddcs_runtime
    PRIVATE
        src/fd_handler_table.cpp
        src/reactor.cpp
        src/signal_fd.cpp
        src/timer_handler_table.cpp
        src/timer_queue.cpp
        src/timer_scheduler.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/runtime/detail/fd_handler_table.hpp
                include/ddcs/runtime/detail/timer_handler_table.hpp
                include/ddcs/runtime/fd_handler.hpp
                include/ddcs/runtime/reactor.hpp
                include/ddcs/runtime/signal_fd.hpp
                include/ddcs/runtime/timer_handler.hpp
                include/ddcs/runtime/timer_id.hpp
                include/ddcs/runtime/detail/timer_queue.hpp
                include/ddcs/runtime/timer_scheduler.hpp
)
target_link_libraries(
    ddcs_runtime
    PRIVATE compile_option ddcs::logger
    PUBLIC ddcs::common
)

add_subdirectory(test/unit)
