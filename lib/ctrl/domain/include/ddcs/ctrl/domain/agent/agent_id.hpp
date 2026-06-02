#pragma once

#include "ddcs/common/strong_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::domain::agent {

// Agent 의 영속 식별자(재접속 가로질러 안정). AgentRegistry 가 발급.
using AgentId = common::StrongId<struct AgentIdTag, std::uint64_t>;

} // namespace ddcs::ctrl::domain::agent
