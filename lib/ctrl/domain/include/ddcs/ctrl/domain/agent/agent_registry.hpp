#pragma once

#include "ddcs/ctrl/domain/agent/agent.hpp"
#include "ddcs/ctrl/domain/agent/agent_id.hpp"
#include "ddcs/ctrl/domain/agent/agent_uuid.hpp"

#include <string>
#include <unordered_map>

#include <cstddef>
#include <cstdint>

namespace ddcs::ctrl::domain::agent {

// Agent 식별의 영속 저장소: uuid <-> id 매핑.
// 동일 uuid 의 재등록 시 동일 AgentId 가 보장된다(identity persistence across reconnects).
// 현재 connection 과의 binding 은 app::session 이 별도로 관리한다.
class AgentRegistry {
public:
    AgentRegistry() = default;
    ~AgentRegistry() = default;

    AgentRegistry(AgentRegistry const&) = delete;
    AgentRegistry& operator=(AgentRegistry const&) = delete;
    AgentRegistry(AgentRegistry&&) noexcept = delete;
    AgentRegistry& operator=(AgentRegistry&&) noexcept = delete;

    // 주어진 uuid 에 대응하는 Agent 를 반환. 없으면 새로 생성(다음 AgentId 할당).
    // unordered_map 요소는 rehash 에도 안정적이므로 const& 반환 안전.
    Agent const& find_or_create(AgentUuid const& uuid);

    // 등록 시 선언된 가변 속성(group/version) 갱신. identity(id/uuid)는 불변. 미지의 id 는 무시.
    void set_attributes(AgentId id, std::string group, std::string version);

    // 최근 Status 텔레메트리(mode/load/temp) 반영. 미지의 id 는 무시.
    void update_telemetry(AgentId id, device::Mode mode, double load, double temp);

    // 조회 (없으면 nullptr).
    Agent const* find_by_uuid(AgentUuid const& uuid) const;
    Agent const* find_by_id(AgentId id) const;

    std::size_t size() const noexcept { return by_uuid_.size(); }

private:
    std::uint64_t next_agent_id_{0};
    std::unordered_map<AgentUuid, Agent> by_uuid_;
    std::unordered_map<AgentId, AgentUuid> id_to_uuid_; // 역인덱스 (id 조회)
};

} // namespace ddcs::ctrl::domain::agent
