# 라이브러리 경로
add_subdirectory(domain)
add_subdirectory(port)
add_subdirectory(infra)
add_subdirectory(app)

# 조립 루트 (facade)
add_library(ddcs_ctrl STATIC)
add_library(ddcs::ctrl ALIAS ddcs_ctrl)
target_sources(
    ddcs_ctrl
    PRIVATE src/controller.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES include/ddcs/ctrl/controller.hpp
)
target_link_libraries(
    ddcs_ctrl
    PRIVATE
        compile_option
        ddcs::ctrl::app
        ddcs::ctrl::domain
        ddcs::ctrl::infra
        ddcs::runtime
        ddcs::json
    PUBLIC ddcs::common ddcs::device ddcs::logger
)

# 테스트 경로
add_subdirectory(test/unit)
