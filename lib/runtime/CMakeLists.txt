add_library(ddcs_runtime STATIC)
add_library(ddcs::runtime ALIAS ddcs_runtime)

target_sources(
    ddcs_runtime
    PRIVATE
        src/fd_dispatch_table.cpp
        src/reactor.cpp
        src/signal_source.cpp
        src/timer_queue.cpp
        src/timer_source.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/runtime/fd_dispatch_table.hpp
                include/ddcs/runtime/fd_handler.hpp
                include/ddcs/runtime/reactor.hpp
                include/ddcs/runtime/signal_source.hpp
                include/ddcs/runtime/timer_handler.hpp
                include/ddcs/runtime/timer_id.hpp
                include/ddcs/runtime/timer_queue.hpp
                include/ddcs/runtime/timer_source.hpp
)
target_link_libraries(
    ddcs_runtime
    PRIVATE compile_option ddcs::logger
    PUBLIC ddcs::common
)

# 테스트 경로
add_subdirectory(test/unit)
