#pragma once

#include "ddcs/common/strong_id.hpp"

#include <cstdint>

namespace ddcs::runtime {

/**
 * @brief 타이머 식별자 클래스
 *
 */
using TimerId = common::StrongId<struct TimerIdTag, std::uint64_t>;

} // namespace ddcs::runtime
