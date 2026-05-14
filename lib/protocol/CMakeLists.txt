add_library(ddcs_protocol STATIC)
add_library(ddcs::protocol ALIAS ddcs_protocol)

target_sources(
    ddcs_protocol
    PRIVATE src/frame.cpp src/message.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/ddcs/protocol/frame.hpp
                include/ddcs/protocol/message.hpp
)
target_link_libraries(ddcs_protocol PRIVATE compile_option)

# 테스트 경로
add_subdirectory(test)
