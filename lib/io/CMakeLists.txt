add_library(ddcs_io STATIC)
add_library(ddcs::io ALIAS ddcs_io)

target_sources(
    ddcs_io
    PRIVATE src/reactor.cpp src/signal_source.cpp src/timer_scheduler.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/io/channel.hpp
                include/ddcs/io/channel_events.hpp
                include/ddcs/io/channel_handler.hpp
                include/ddcs/io/reactor.hpp
                include/ddcs/io/signal_source.hpp
                include/ddcs/io/timer_handler.hpp
                include/ddcs/io/timer_id.hpp
                include/ddcs/io/timer_scheduler.hpp
                include/ddcs/io/detail/channel_registry.hpp
                include/ddcs/io/detail/timer_queue.hpp
                include/ddcs/io/detail/timer_registration_table.hpp
)
target_link_libraries(ddcs_io PRIVATE compile_option PUBLIC ddcs::common)

add_subdirectory(test/unit)
