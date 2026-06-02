# 공유 도메인 어휘(shared kernel). 현재 Mode enum 1개 — 순수 헤더라 INTERFACE.
add_library(ddcs_device INTERFACE)
add_library(ddcs::device ALIAS ddcs_device)

target_sources(
    ddcs_device
    INTERFACE
        FILE_SET HEADERS BASE_DIRS include FILES include/ddcs/device/mode.hpp
)
