include_guard(GLOBAL)

include(DdcsBuildOptions)

if(NOT DDCS_ENABLE_COVERAGE)
    return()
endif()

if(DDCS_ENABLE_ASAN OR DDCS_ENABLE_TSAN)
    message(
        FATAL_ERROR
        "DDCS_ENABLE_COVERAGE cannot be enabled with DDCS_ENABLE_ASAN or DDCS_ENABLE_TSAN"
    )
endif()

if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "DDCS_ENABLE_COVERAGE requires CMAKE_BUILD_TYPE=Debug")
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "DDCS_ENABLE_COVERAGE requires GCC/gcov")
endif()

target_compile_options(ddcs_build_options INTERFACE -O0 -g --coverage)

target_link_options(ddcs_build_options INTERFACE --coverage)
