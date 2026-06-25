#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

namespace ddcs::ctrl::app::transport::port {

using MessageBuffer = ddcs::common::PoolHandle<ddcs::common::LinearBuffer>;

} // namespace ddcs::ctrl::app::transport::port
