#pragma once

#include "ddcs/device/mode.hpp"

#include <variant>

namespace ddcs::ctrl::app::device::port {

// controller가 발행하는 typed 명령 어휘. 계열 = variant 대안이고 wire 인코딩은 어댑터 책임이다.
struct SetMode {
    ddcs::device::Mode mode;
};

using Command = std::variant<SetMode>;

} // namespace ddcs::ctrl::app::device::port
