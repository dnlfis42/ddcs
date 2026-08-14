#include "ddcs/ctrl/app/device/policy_service.hpp"

#include "ddcs/ctrl/app/device/group_aggregate.hpp"
#include "ddcs/ctrl/domain/device_shadow.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/event.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ddcs::ctrl::app::device {

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
        auto const* busy = g.find("high_load_mode");
        auto const* idle = g.find("low_load_mode");
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

        // 온도 override (선택): high_temp/resume_temp/high_temp_mode 셋 다 있으면 활성, 일부면
        // malformed
        std::optional<domain::ThermalRule> thermal;
        auto const* ht = g.find("high_temp");
        auto const* rt = g.find("resume_temp");
        auto const* htm = g.find("high_temp_mode");
        if (ht != nullptr || rt != nullptr || htm != nullptr) {
            if (ht == nullptr || rt == nullptr || htm == nullptr) {
                ok = false;
                return;
            }

            auto const htv = ht->as_double();
            auto const rtv = rt->as_double();
            auto const htms = htm->as_string();
            if (!htv || !rtv || !htms) {
                ok = false;
                return;
            }

            auto const htmm = ddcs::device::parse_mode(*htms);
            if (!htmm) {
                ok = false;
                return;
            }

            thermal = domain::ThermalRule{
                .high_temp = *htv, .resume_temp = *rtv, .high_temp_mode = *htmm
            };
        }

        // 밴드 불변식(load + thermal)은 도메인이 강제한다
        auto rule = domain::GroupRule::create(*hv, *lv, *bm, *im, thermal);
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
    // regime/thermal은 히스테리시스 latch라 보존한다: 비우면 과열 latch가 resume_temp 전에 조기
    // 해제되고, 데드밴드(low<avg<high) group이 새 룰의 mode를 재적용하지 못한다.
    commanded_.clear();
}

void PolicyService::on_device_released(domain::DeviceId device) {
    commanded_.erase(device);
    thermal_.erase(device);
}

void PolicyService::evaluate(common::Clock::time_point now) {
    if (policy_.empty()) {
        return;
    }

    // 1. group별 평균 load 집계 (load 정책은 그룹 단위; 끊긴 device의 stale Shadow 제외).
    //    집계 규칙은 aggregate_groups가 소유하고 메트릭 노출과 공유한다.
    auto const agg = aggregate_groups(active_devices_, devices_, policy_);

    // 2. group별 load regime. 전이는 GroupRule::next_regime이 판정하고, 전환 로그만 여기서
    //    group 단위로 남긴다(1회).
    struct GroupState {
        domain::GroupRule const* rule;
        domain::Regime regime;
    };
    std::unordered_map<std::string, GroupState> gstate;
    policy_.for_each([&](std::string const& group, domain::GroupRule const& rule) {
        auto const it = agg.find(group);
        if (it == agg.end() || it->second.device_count == 0) {
            return; // active device 없는 group은 건너뛴다
        }
        double const avg = it->second.load_sum / static_cast<double>(it->second.device_count);
        domain::Regime& regime = regime_[group];
        domain::Regime const previous = regime;
        regime = rule.next_regime(regime, avg);
        if (regime != previous) {
            LOG_POLICY_REGIME_UPDATE(group, domain::to_string(regime), avg);
        }
        gstate.emplace(group, GroupState{&rule, regime});
    });

    // 3. device별 thermal 전이와 effective 합성은 GroupRule이 판정한다. 바뀐 device만 명령.
    //    (dispatch가 순회 중 disconnect를 부를 수 있어 대상을 모은 뒤 순회 밖에서 발송)
    pending_.clear();
    active_devices_.for_each_active([&](domain::DeviceId id) {
        auto const* shadow = devices_.find(id);
        if (shadow == nullptr || !shadow->status) {
            return; // Shadow 없음 또는 미관측. 관측 없이는 판단도 명령도 하지 않는다.
        }
        auto const git = gstate.find(shadow->group);
        if (git == gstate.end()) {
            return; // 정책 없는/비활성 group
        }
        domain::GroupRule const& rule = *git->second.rule;

        domain::Thermal& thermal = thermal_[id];
        domain::Thermal const previous = thermal;
        thermal = rule.next_thermal(thermal, shadow->status->temp);
        if (thermal != previous) {
            // latch에 들어갈 때와 풀릴 때를 같이 남긴다. 들어간 기록만 있으면 언제 정상으로
            // 돌아왔는지 알 길이 없다.
            LOG_POLICY_THERMAL_UPDATE(
                id.to_string(), domain::to_string(thermal), shadow->status->temp
            );
        }

        auto const effective = rule.effective_mode(git->second.regime, thermal, commanded_[id]);
        if (!effective) {
            return; // regime 미확정 + thermal latch 없음: 아직 결정 없음
        }
        auto& commanded = commanded_[id];
        if (commanded == effective) {
            return; // effective 안 바뀜 -> 무명령 (스팸 없음)
        }
        commanded = effective;
        pending_.emplace_back(id, *effective);
    });

    for (auto const& [device, mode] : pending_) {
        command_one(device, mode, now);
    }
}

void PolicyService::command_one(
    domain::DeviceId device, ddcs::device::Mode mode, common::Clock::time_point now
) {
    // 미연결 등 송신 실패는 dispatch가 invalid 반환 + WARN. 다음 평가가 자연 보상.
    // Mode -> wire byte 매핑은 커널(encode_mode) 경유가 계약이다(캐스팅 금지).
    commands_.dispatch(
        device, wire::command::SetMode{.mode = ddcs::device::encode_mode(mode)}, now
    );
}

} // namespace ddcs::ctrl::app::device
