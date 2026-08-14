#pragma once

#include "ddcs/common/uuid.hpp"

namespace ddcs::ctrl::domain {

// Device의 영속 신원. DeviceShadow의 키이며 재접속을 넘어 지속된다.
using DeviceId = common::Uuid;

} // namespace ddcs::ctrl::domain
