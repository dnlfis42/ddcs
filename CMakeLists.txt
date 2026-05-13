# 프로젝트
cmake_minimum_required(VERSION 3.25)
project(ddcs VERSION 1.0.0 LANGUAGES CXX)

# 기본 빌드 타입 (preset 없이 호출됐을 때의 안전망)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(
        CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel"
    )
endif()

# C++ 표준
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# IDE/clangd 연동
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 컬러 진단
set(CMAKE_COLOR_DIAGNOSTICS ON)

# Preset 토글용 옵션
option(DDCS_WARNING_AS_ERROR "Treat warning as error" OFF)
option(DDCS_ENABLE_ASAN "Enable ASan + UBSan" OFF)
option(DDCS_ENABLE_TSAN "Enable TSan" OFF)

if(DDCS_ENABLE_ASAN AND DDCS_ENABLE_TSAN)
    message(
        FATAL_ERROR
        "DDCS_ENABLE_ASAN and DDCS_ENABLE_TSAN cannot be enabled together"
    )
endif()

message(STATUS "[ddcs] Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "[ddcs] Warning as error: ${DDCS_WARNING_AS_ERROR}")
message(STATUS "[ddcs] ASan: ${DDCS_ENABLE_ASAN}  TSan: ${DDCS_ENABLE_TSAN}")

# 컴파일 옵션 (INTERFACE 타깃으로 격리 — 외부 의존성에 새지 않게)
add_library(compile_option INTERFACE)
target_compile_options(
    compile_option
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        $<$<BOOL:${DDCS_WARNING_AS_ERROR}>:-Werror>
        $<$<BOOL:${DDCS_ENABLE_ASAN}>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>
        $<$<BOOL:${DDCS_ENABLE_TSAN}>:-fsanitize=thread;-fno-omit-frame-pointer>
)
target_link_options(
    compile_option
    INTERFACE
        $<$<BOOL:${DDCS_ENABLE_ASAN}>:-fsanitize=address;-fsanitize=undefined>
        $<$<BOOL:${DDCS_ENABLE_TSAN}>:-fsanitize=thread>
)

# ccache (있으면 사용)
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
    message(STATUS "[ddcs] ccache enabled: ${CCACHE}")
endif()

# 출력 경로
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 의존성
include(FetchContent)

# gtest
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    SYSTEM
)
set(INSTALL_GTEST OFF)
FetchContent_MakeAvailable(googletest)

# benchmark
FetchContent_Declare(
    benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
    SYSTEM
)
set(BENCHMARK_ENABLE_TESTING OFF)
set(BENCHMARK_ENABLE_INSTALL OFF)
FetchContent_MakeAvailable(benchmark)

# 테스트 활성화
enable_testing()

# 라이브러리 경로
add_subdirectory(lib)

# 실행 파일 경로
add_subdirectory(app/agent)
add_subdirectory(app/controller)

# 테스트 경로
add_subdirectory(test)
