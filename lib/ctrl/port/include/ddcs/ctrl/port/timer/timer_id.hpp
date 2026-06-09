#pragma once

#include "ddcs/common/strong_value.hpp"

#include <cstdint>

namespace ddcs::ctrl::port::timer {

using TimerId = common::StrongValue<struct TimerIdTag, std::uint64_t>;

} // namespace ddcs::ctrl::port::timer
