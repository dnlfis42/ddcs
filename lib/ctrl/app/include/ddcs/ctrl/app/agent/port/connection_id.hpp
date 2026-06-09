#pragma once

#include "ddcs/common/strong_value.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

using ConnectionId = common::StrongValue<struct ConnectionIdTag, std::uint64_t>;

} // namespace ddcs::ctrl::app::agent::port
