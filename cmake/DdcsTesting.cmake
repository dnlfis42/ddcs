include_guard(GLOBAL)

function(ddcs_add_unit_test target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LIBRARIES")

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "ddcs_add_unit_test requires SOURCES")
    endif()

    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(
        ${target}
        PRIVATE ddcs::build_options ${ARG_LIBRARIES} GTest::gtest_main
    )
    add_test(NAME ${target} COMMAND ${target})
endfunction()
