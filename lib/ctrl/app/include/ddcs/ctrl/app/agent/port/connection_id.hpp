#pragma once

#include "ddcs/common/strong_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

using ConnectionId = common::StrongId<struct ConnectionIdTag, std::uint64_t>;

} // namespace ddcs::ctrl::app::agent::port
