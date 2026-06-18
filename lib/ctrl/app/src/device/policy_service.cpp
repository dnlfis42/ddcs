#include "ddcs/ctrl/app/device/policy_service.hpp"

#include "ddcs/ctrl/domain/device.hpp"
#include "ddcs/device/command.hpp"
#include "ddcs/logger/log.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ddcs::ctrl::app::device {

namespace cmd = ddcs::device;

std::optional<domain::GroupPolicy> parse_policy(json::Value const& root) {
    auto const* groups = root.find("groups");
    if (groups == nullptr || !groups->is_object()) {
        return std::nullopt;
    }
    domain::GroupPolicy policy;
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
        auto const bm = ddcs::device::parse_mode(*bs);
        auto const im = ddcs::device::parse_mode(*is);
        if (!bm || !im) {
            ok = false;
            return;
        }
        auto rule =
            domain::GroupRule::try_make(*hv, *lv, *bm, *im); // 밴드 불변식은 도메인이 강제한다
        if (!rule) {
            ok = false;
            return;
        }
        policy.set(std::string{name}, *rule);
    });
    if (!ok) {
        return std::nullopt;
    }
    return policy;
}

void PolicyService::set_policy(domain::GroupPolicy policy) {
    policy_ = std::move(policy);
    regime_.clear(); // 새 정책이면 재평가(이전 regime 무효)
}

void PolicyService::evaluate(common::Clock::time_point now) {
    if (policy_.empty()) {
        return;
    }
    // 1. active device의 group별 load 집계 (끊긴 device의 stale 트윈 제외)
    struct Agg {
        double sum{};
        int count{};
    };
    std::unordered_map<std::string, Agg> agg;
    roster_.for_each_active([&](domain::DeviceId id) {
        auto const* twin = devices_.find(id);
        if (twin == nullptr || twin->group.empty()) {
            return;
        }
        auto& a = agg[twin->group];
        a.sum += twin->status.load;
        ++a.count;
    });
    // 2. 정책 그룹별 히스테리시스 평가 후 regime 전환 시에만 명령
    policy_.for_each([&](std::string const& group, domain::GroupRule const& rule) {
        auto const it = agg.find(group);
        if (it == agg.end() || it->second.count == 0) {
            return; // active device 없는 그룹은 skip
        }
        double const avg = it->second.sum / static_cast<double>(it->second.count);
        Regime& regime = regime_[group];
        if (avg > rule.high_load() && regime != Regime::busy) {
            regime = Regime::busy;
            LOG_WARN("policy.busy", logger::kv("group", group), logger::kv("avg_load", avg));
            command_group(group, rule.busy_mode(), now);
        } else if (avg < rule.low_load() && regime != Regime::idle) {
            regime = Regime::idle;
            LOG_INFO("policy.idle", logger::kv("group", group), logger::kv("avg_load", avg));
            command_group(group, rule.idle_mode(), now);
        }
    });
}

void PolicyService::command_group(
    std::string const& group, ddcs::device::Mode mode, common::Clock::time_point now
) {
    // CAUTION: dispatch는 송신 실패 시 동기 disconnect로 roster를 순회 중 변형할 수 있다.
    // CAUTION  그래서 대상을 먼저 모으고 for_each_active 밖에서 발송한다(DeviceRoster 포트 계약).
    targets_.clear();
    roster_.for_each_active([&](domain::DeviceId id) {
        auto const* twin = devices_.find(id);
        if (twin != nullptr && twin->group == group) {
            targets_.push_back(id);
        }
    });
    for (auto const id : targets_) {
        auto buf = commands_.make_command_buffer();
        auto const written = cmd::encode_set_mode(buf->tailroom_span(), mode);
        if (!written || !buf->try_commit(*written)) {
            LOG_ERROR("policy.encode_fail", logger::kv("device", id.to_string())); // 버그 신호
            continue;
        }
        // 미연결 등 송신 실패는 dispatch가 invalid 반환 + WARN. 다음 전환/재평가가 자연 보상.
        commands_.dispatch(
            id, static_cast<std::uint8_t>(cmd::CommandType::set_mode), std::move(buf), now
        );
    }
}

} // namespace ddcs::ctrl::app::device
