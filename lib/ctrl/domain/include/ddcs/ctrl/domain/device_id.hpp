#pragma once

#include "ddcs/common/strong_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::domain {

// Device의 영속 식별자(재접속 가로질러 안정).
using DeviceId = common::StrongId<struct DeviceIdTag, std::uint64_t>;

} // namespace ddcs::ctrl::domain
