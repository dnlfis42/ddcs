include_guard(GLOBAL)

if(NOT TARGET ddcs_cxx_standard)
    add_library(ddcs_cxx_standard INTERFACE)
    add_library(ddcs::cxx_standard ALIAS ddcs_cxx_standard)
endif()

if(NOT TARGET ddcs_warning_options)
    add_library(ddcs_warning_options INTERFACE)
    add_library(ddcs::warning_options ALIAS ddcs_warning_options)
endif()

if(NOT TARGET ddcs_build_options)
    add_library(ddcs_build_options INTERFACE)
    add_library(ddcs::build_options ALIAS ddcs_build_options)
endif()

target_compile_features(ddcs_cxx_standard INTERFACE cxx_std_20)

target_compile_options(
    ddcs_warning_options
    INTERFACE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wsign-conversion>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/permissive->"
        "$<$<AND:$<BOOL:${DDCS_WARNING_AS_ERROR}>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-Werror>"
        "$<$<AND:$<BOOL:${DDCS_WARNING_AS_ERROR}>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/WX>"
)

target_link_libraries(
    ddcs_build_options
    INTERFACE ddcs::cxx_standard ddcs::warning_options
)
