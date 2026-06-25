#pragma once

#include "ddcs/device/mode.hpp"

namespace ddcs::ctrl::domain {

// Agent가 보고하고 controller가 Shadow에 보관하는 최신 device 텔레메트리 스냅샷
// - device 커널의 공유 어휘 Mode만 빌리고, 집합체는 컨텍스트(controller)가 소유한다.
struct Status {
    device::Mode mode{};
    double load{};
    double temp{};
};

} // namespace ddcs::ctrl::domain
