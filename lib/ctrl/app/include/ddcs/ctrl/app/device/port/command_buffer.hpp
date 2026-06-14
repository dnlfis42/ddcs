#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

namespace ddcs::ctrl::app::device::port {

// 명령 조립용 buffer. frame/command 헤더 headroom이 예약된 채 대여된다.
using CommandBuffer = ddcs::common::PoolHandle<ddcs::common::LinearBuffer>;

} // namespace ddcs::ctrl::app::device::port
