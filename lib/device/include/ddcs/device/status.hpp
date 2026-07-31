#pragma once

#include "ddcs/device/mode.hpp"

namespace ddcs::device {

// Device 상태 스냅샷
struct Status {
    Mode mode{};   // 작동 모드
    double load{}; // 부하 가상 지표
    double temp{}; // 온도
};

} // namespace ddcs::device
