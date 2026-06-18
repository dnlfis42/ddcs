cmake_minimum_required(VERSION 3.25)
project(ddcs VERSION 1.0.0 LANGUAGES CXX)

# 프로젝트 공통 CMake 모듈
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(DdcsProjectOptions)
include(DdcsCompilerCache)
include(DdcsBuildOptions)
include(DdcsSanitizers)
include(DdcsCoverage)
include(DdcsDependencies)
include(DdcsTesting)

# 테스트
enable_testing()

# 타깃
add_subdirectory(lib)
add_subdirectory(apps)
add_subdirectory(test)
