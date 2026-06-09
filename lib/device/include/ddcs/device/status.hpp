#pragma once

#include "ddcs/device/mode.hpp"

namespace ddcs::device {

// Agent가 보고하고 Controller가 보관하는 최신 device snapshot
struct Status {
    Mode mode{};
    double load{};
    double temp{};
};

} // namespace ddcs::device
