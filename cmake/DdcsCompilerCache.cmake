include_guard(GLOBAL)

option(DDCS_ENABLE_COMPILER_CACHE "Use ccache when available" ON)

if(DDCS_ENABLE_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE ccache)
    if(CCACHE)
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
        message(STATUS "[ddcs] ccache enabled: ${CCACHE}")
    endif()
endif()
