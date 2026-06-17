include_guard(GLOBAL)

if(DDCS_ENABLE_ASAN AND DDCS_ENABLE_TSAN)
    message(
        FATAL_ERROR
        "DDCS_ENABLE_ASAN and DDCS_ENABLE_TSAN cannot be enabled together"
    )
endif()

if(
    (DDCS_ENABLE_ASAN OR DDCS_ENABLE_TSAN)
    AND NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
)
    message(FATAL_ERROR "DDCS sanitizers require GCC, Clang, or AppleClang")
endif()

if(NOT TARGET compile_option)
    add_library(compile_option INTERFACE)
endif()

target_compile_options(
    compile_option
    INTERFACE
        "$<$<BOOL:${DDCS_ENABLE_ASAN}>:-fsanitize=address;-fsanitize=undefined;-fno-omit-frame-pointer>"
        "$<$<BOOL:${DDCS_ENABLE_TSAN}>:-fsanitize=thread;-fno-omit-frame-pointer>"
)

target_link_options(
    compile_option
    INTERFACE
        "$<$<BOOL:${DDCS_ENABLE_ASAN}>:-fsanitize=address;-fsanitize=undefined>"
        "$<$<BOOL:${DDCS_ENABLE_TSAN}>:-fsanitize=thread>"
)
