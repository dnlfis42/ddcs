#pragma once

#include "ddcs/common/uuid.hpp"

namespace ddcs::ctrl::domain::agent {

// Agent 의 식별 토큰. v4 random Uuid 가정(충돌 확률 0). wire 의 agent_uuid 와 동일.
using AgentUuid = common::Uuid;

} // namespace ddcs::ctrl::domain::agent
