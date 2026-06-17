include_guard(GLOBAL)

if(NOT TARGET compile_option)
    add_library(compile_option INTERFACE)
endif()

target_compile_features(compile_option INTERFACE cxx_std_20)

target_compile_options(
    compile_option
    INTERFACE
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wsign-conversion>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4;/permissive->"
        "$<$<AND:$<BOOL:${DDCS_WARNING_AS_ERROR}>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-Werror>"
        "$<$<AND:$<BOOL:${DDCS_WARNING_AS_ERROR}>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/WX>"
)
