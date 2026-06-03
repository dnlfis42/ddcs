#pragma once

#include "ddcs/device/mode.hpp"

namespace ddcs::device {

// 디바이스의 관측 상태(스냅샷) - 공유 도메인 어휘(shared kernel).
// agent 가 보고하고 controller 가 트윈으로 보관한다. 최신값만(시계열 아님).
//  - mode: 디바이스가 보고한 현재 모드(정책 출력 피드백)
//  - load: 부하 가상지표 0~100(정책 입력 신호)
//  - temp: 온도 C
// wire 의 proto::msg::Status(직렬화 DTO)와는 다른 계층 - 이건 도메인 값.
struct Status {
    Mode mode{};
    double load{};
    double temp{};
};

} // namespace ddcs::device
