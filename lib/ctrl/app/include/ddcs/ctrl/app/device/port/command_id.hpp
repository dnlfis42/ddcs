#pragma once

#include "ddcs/common/strong_value.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::device::port {

// 명령 발송/응답 상관 토큰. CommandService가 발급하고 wire(dacp)에서는 raw u64로 오간다.
using CommandId = common::StrongValue<struct CommandIdTag, std::uint64_t>;

} // namespace ddcs::ctrl::app::device::port
