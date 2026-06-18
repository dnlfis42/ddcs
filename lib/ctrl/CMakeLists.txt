# 하위 라이브러리
add_subdirectory(domain)
add_subdirectory(app)
add_subdirectory(infra)

# 공개 facade 타깃
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
        ddcs::build_options
        ddcs::ctrl::infra
        ddcs::ctrl::app
        ddcs::ctrl::domain
        ddcs::io
        ddcs::json
    PUBLIC ddcs::cxx_standard ddcs::logger ddcs::device ddcs::common
)

add_subdirectory(test/unit)
