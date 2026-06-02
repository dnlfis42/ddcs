#include "ddcs/ctrl/domain/agent/agent_registry.hpp"

#include <utility>

namespace ddcs::ctrl::domain::agent {

Agent const& AgentRegistry::find_or_create(AgentUuid const& uuid) {
    auto it = by_uuid_.find(uuid);
    if (it != by_uuid_.end()) {
        return it->second;
    }
    AgentId const new_id{++next_agent_id_};
    // group/version 은 빈 문자열로 시작 - 등록 시 set_attributes 가 채운다.
    auto [ins_it, inserted] = by_uuid_.emplace(uuid, Agent{.id = new_id, .uuid = uuid});
    id_to_uuid_.emplace(new_id, uuid);
    return ins_it->second;
}

void AgentRegistry::set_attributes(AgentId id, std::string group, std::string version) {
    auto const idx = id_to_uuid_.find(id);
    if (idx == id_to_uuid_.end()) {
        return; // 미지의 id - 방어적 무시
    }
    auto& agent = by_uuid_.at(idx->second);
    agent.group = std::move(group);
    agent.version = std::move(version);
}

void AgentRegistry::update_telemetry(AgentId id, device::Mode mode, double load, double temp) {
    auto const idx = id_to_uuid_.find(id);
    if (idx == id_to_uuid_.end()) {
        return; // 미지의 id - 방어적 무시
    }
    auto& agent = by_uuid_.at(idx->second);
    agent.mode = mode;
    agent.load = load;
    agent.temp = temp;
}

Agent const* AgentRegistry::find_by_uuid(AgentUuid const& uuid) const {
    auto const it = by_uuid_.find(uuid);
    return it == by_uuid_.end() ? nullptr : &it->second;
}

Agent const* AgentRegistry::find_by_id(AgentId id) const {
    auto const idx = id_to_uuid_.find(id);
    if (idx == id_to_uuid_.end()) {
        return nullptr;
    }
    auto const it = by_uuid_.find(idx->second);
    return it == by_uuid_.end() ? nullptr : &it->second;
}

} // namespace ddcs::ctrl::domain::agent
