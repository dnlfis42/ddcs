#pragma once

#include "ddcs/common/strong_id.hpp"

#include <cstdint>

namespace ddcs::io {

using TimerToken = common::StrongId<struct TimerTokenTag, std::uint64_t>;

} // namespace ddcs::io
