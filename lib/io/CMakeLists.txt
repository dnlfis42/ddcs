add_library(ddcs_io STATIC)
add_library(ddcs::io ALIAS ddcs_io)

target_sources(
    ddcs_io
    PRIVATE src/reactor.cpp src/signal_source.cpp src/timer_scheduler.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/io/detail/channel_registry.hpp
                include/ddcs/io/detail/timer_queue.hpp
                include/ddcs/io/detail/timer_registry.hpp
                include/ddcs/io/channel.hpp
                include/ddcs/io/channel_events.hpp
                include/ddcs/io/channel_handler.hpp
                include/ddcs/io/fd.hpp
                include/ddcs/io/reactor.hpp
                include/ddcs/io/signal_source.hpp
                include/ddcs/io/sys_result.hpp
                include/ddcs/io/throw_errno.hpp
                include/ddcs/io/timer_handler.hpp
                include/ddcs/io/timer_scheduler.hpp
                include/ddcs/io/timer_token.hpp
)
target_link_libraries(
    ddcs_io
    PRIVATE ddcs::build_options
    PUBLIC ddcs::cxx_standard ddcs::common
)

add_subdirectory(test/unit)
