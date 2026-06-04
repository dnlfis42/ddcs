#include "ddcs/ctrl/app/policy/policy_service.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/logger/log.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ddcs::ctrl::app::policy {

using ddcs::ctrl::app::session::Session;
using ddcs::ctrl::app::session::State;

std::optional<domain::Policy> parse_policy(json::Value const& root) {
    auto const* groups = root.find("groups");
    if (groups == nullptr || !groups->is_object()) {
        return std::nullopt;
    }
    domain::Policy policy;
    bool ok = true;
    groups->for_each_member([&](std::string_view name, json::Value const& g) {
        auto const* high = g.find("high_load");
        auto const* low = g.find("low_load");
        auto const* busy = g.find("busy_mode");
        auto const* idle = g.find("idle_mode");
        if (high == nullptr || low == nullptr || busy == nullptr || idle == nullptr) {
            ok = false;
            return;
        }
        auto const hv = high->as_double();
        auto const lv = low->as_double();
        auto const bs = busy->as_string();
        auto const is = idle->as_string();
        if (!hv || !lv || !bs || !is) {
            ok = false;
            return;
        }
        auto const bm = device::from_string(*bs);
        auto const im = device::from_string(*is);
        if (!bm || !im) {
            ok = false;
            return;
        }
        policy.set(
            std::string{name}, domain::GroupRule{.high_load = *hv, .low_load = *lv, .busy_mode = *bm, .idle_mode = *im}
        );
    });
    if (!ok) {
        return std::nullopt;
    }
    return policy;
}

void PolicyService::set_policy(domain::Policy policy) {
    policy_ = std::move(policy);
    regime_.clear(); // 새 정책 -> 재평가(이전 regime 무효)
}

void PolicyService::evaluate() {
    if (policy_.empty()) {
        return;
    }
    // 1. 활성 agent 의 group별 load 집계 (끊긴 agent 의 stale load 제외).
    struct Agg {
        double sum{};
        int count{};
    };
    std::unordered_map<std::string, Agg> agg;
    sessions_.for_each([&](auto /*conn*/, Session const& s) {
        if (s.state != State::active) {
            return;
        }
        auto const* agent = registry_.find_by_id(s.agent);
        if (agent == nullptr || agent->group.empty()) {
            return;
        }
        auto& a = agg[agent->group];
        a.sum += agent->load;
        ++a.count;
    });
    // 2. 정책 그룹별 히스테리시스 평가 -> regime 전환 시에만 명령.
    policy_.for_each([&](std::string const& group, domain::GroupRule const& rule) {
        auto const it = agg.find(group);
        if (it == agg.end() || it->second.count == 0) {
            return; // 활성 agent 없는 그룹 -> skip
        }
        double const avg = it->second.sum / static_cast<double>(it->second.count);
        Regime& reg = regime_[group];
        if (avg > rule.high_load && reg != Regime::busy) {
            reg = Regime::busy;
            LOG_WARN("policy.busy", ddcs::logger::kv("group", group), ddcs::logger::kv("avg_load", avg));
            command_group(group, rule.busy_mode);
        } else if (avg < rule.low_load && reg != Regime::idle) {
            reg = Regime::idle;
            LOG_INFO("policy.idle", ddcs::logger::kv("group", group), ddcs::logger::kv("avg_load", avg));
            command_group(group, rule.idle_mode);
        }
    });
}

void PolicyService::command_group(std::string const& group, device::Mode mode) {
    sessions_.for_each([&](auto /*conn*/, Session const& s) {
        if (s.state != State::active) {
            return;
        }
        auto const* agent = registry_.find_by_id(s.agent);
        if (agent == nullptr || agent->group != group) {
            return;
        }
        ops_.set_mode(agent->uuid, mode); // #8 에서 부분실패 보상(재시도/백오프) 추가 예정
    });
}

} // namespace ddcs::ctrl::app::policy
