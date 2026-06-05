#pragma once

#include "ddcs/common/strong_id.hpp"

#include <cstdint>

namespace ddcs::runtime {

using TimerId = common::StrongId<struct TimerIdTag, std::uint64_t>;

} // namespace ddcs::runtime
