include_guard(GLOBAL)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(
        CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel"
    )
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DDCS_WARNING_AS_ERROR "Treat warnings as errors" OFF)
option(DDCS_ENABLE_ASAN "Enable ASan + UBSan" OFF)
option(DDCS_ENABLE_TSAN "Enable TSan" OFF)
option(DDCS_ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)
option(DDCS_ENABLE_BENCHMARK "Build benchmarks" OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(CMAKE_CONFIGURATION_TYPES)
    message(STATUS "[ddcs] Build types: ${CMAKE_CONFIGURATION_TYPES}")
else()
    message(STATUS "[ddcs] Build type: ${CMAKE_BUILD_TYPE}")
endif()

message(STATUS "[ddcs] Warning as error: ${DDCS_WARNING_AS_ERROR}")
message(STATUS "[ddcs] Address Sanitizer: ${DDCS_ENABLE_ASAN}")
message(STATUS "[ddcs] Thread Sanitizer: ${DDCS_ENABLE_TSAN}")
message(STATUS "[ddcs] Coverage: ${DDCS_ENABLE_COVERAGE}")
message(STATUS "[ddcs] Benchmark: ${DDCS_ENABLE_BENCHMARK}")
