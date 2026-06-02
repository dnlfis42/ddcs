#pragma once

#include "ddcs/common/strong_id.hpp"

#include <cstdint>

namespace ddcs::io {

/**
 * @brief 타이머 식별자 클래스
 *
 */
using TimerId = common::StrongId<struct TimerIdTag, std::uint64_t>;

} // namespace ddcs::io
