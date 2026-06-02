#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/ctrl/domain/agent/agent_uuid.hpp"
#include "ddcs/device/mode.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::ops {

using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::domain::agent::AgentRegistry;
using ddcs::ctrl::domain::agent::AgentUuid;

// operator(운영자) 의도를 agent 명령으로 옮기는 use-case (driving).
// uuid -> AgentId resolve 후 proto::cmd 로 payload 인코드 -> CommandService.dispatch 로 c->a Command 발신.
// (ns 'operator' 는 키워드라 'ops'.)
class OperatorService {
public:
    OperatorService(AgentRegistry& registry, CommandService& commands) noexcept;

    // SetMode 명령 발신. 반환: 발급된 command_id. 미지 agent/미연결이면 0.
    std::uint64_t set_mode(AgentUuid const& agent_uuid, device::Mode mode);

private:
    AgentRegistry& registry_;
    CommandService& commands_;
    common::ObjectPool<common::LinearBuffer> encode_pool_; // SetMode payload 인코딩용
};

} // namespace ddcs::ctrl::app::ops
