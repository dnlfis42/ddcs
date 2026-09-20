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
        auto const* busy_load = g.find("busy_load");
        auto const* idle_load = g.find("idle_load");
        auto const* busy_mode = g.find("busy_mode");
        auto const* idle_mode = g.find("idle_mode");
        if (busy_load == nullptr || idle_load == nullptr || busy_mode == nullptr ||
            idle_mode == nullptr) {
            ok = false;
            return;
        }

        auto const bl = busy_load->as_double();
        auto const il = idle_load->as_double();
        auto const bs = busy_mode->as_string();
        auto const is = idle_mode->as_string();
        if (!bl || !il || !bs || !is) {
            ok = false;
            return;
        }

        auto const bm = ddcs::device::parse_mode(*bs);
        auto const im = ddcs::device::parse_mode(*is);
        if (!bm || !im) {
            ok = false;
            return;
        }

        // 온도 정책은 선택 사항이다. hot_temp, cool_temp, hot_mode를 모두 지정해야 한다.
        std::optional<domain::ThermalRule> thermal;
        auto const* hot_temp = g.find("hot_temp");
        auto const* cool_temp = g.find("cool_temp");
        auto const* hot_mode = g.find("hot_mode");
        if (hot_temp != nullptr || cool_temp != nullptr || hot_mode != nullptr) {
            if (hot_temp == nullptr || cool_temp == nullptr || hot_mode == nullptr) {
                ok = false;
                return;
            }

            auto const ht = hot_temp->as_double();
            auto const ct = cool_temp->as_double();
            auto const hs = hot_mode->as_string();
            if (!ht || !ct || !hs) {
                ok = false;
                return;
            }

            auto const hm = ddcs::device::parse_mode(*hs);
            if (!hm) {
                ok = false;
                return;
            }

            thermal = domain::ThermalRule{.hot_temp = *ht, .cool_temp = *ct, .hot_mode = *hm};
        }

        // 부하와 온도 임계값의 유효성은 도메인 객체에서 검사한다.
        auto rule = domain::GroupRule::create(*bl, *il, *bm, *im, thermal);
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

PolicyService::~PolicyService() {
    commands_.detach_failure_sink(*this);
}

void PolicyService::on_command_failed(domain::DeviceId device, port::CommandId command) noexcept {
    auto const it = commanded_.find(device);
    if (it != commanded_.end() && it->second.command == command) {
        it->second.failed = true;
    }
}

void PolicyService::set_policy(domain::GroupPolicy policy) {
    policy_ = std::move(policy);
    // 부하와 과열 상태는 유지한다. 과열의 조기 해제를 막고,
    // 부하가 두 임계값 사이에 있어도 기존 상태를 기준으로 새 정책을 적용한다.
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

    // 상태를 보고한 활성 Device의 부하를 Group별로 집계한다. 메트릭도 같은 함수를 사용한다.
    auto const agg = aggregate_groups(active_devices_, devices_, policy_);

    // GroupRule로 Group별 부하 상태를 결정하고, 상태가 바뀌면 기록한다.
    struct GroupState {
        domain::GroupRule const* rule;
        domain::GroupLoadRegime regime;
    };
    std::unordered_map<std::string, GroupState> gstate;
    policy_.for_each([&](std::string const& group, domain::GroupRule const& rule) {
        auto const it = agg.find(group);
        if (it == agg.end() || it->second.device_count == 0) {
            return; // 활성 Device가 없는 Group은 건너뛴다.
        }
        double const avg = it->second.load_sum / static_cast<double>(it->second.device_count);
        domain::GroupLoadRegime& regime = regime_[group];
        domain::GroupLoadRegime const previous = regime;
        regime = rule.next_regime(regime, avg);
        if (regime != previous) {
            LOG_POLICY_REGIME_UPDATE(group, domain::to_string(regime), avg);
        }
        gstate.emplace(group, GroupState{&rule, regime});
    });

    // Device별 과열 상태와 목표 모드를 결정하고, 목표가 바뀌거나 명령이 실패한 대상을 모은다.
    // 전송 중 연결 종료로 목록이 바뀔 수 있으므로 순회를 마친 뒤 전송한다.
    pending_.clear();
    active_devices_.for_each_active([&](domain::DeviceId id) {
        auto const* shadow = devices_.find(id);
        if (shadow == nullptr || !shadow->status) {
            return; // 상태를 보고하지 않은 Device는 평가와 명령 대상에서 제외한다.
        }
        auto const git = gstate.find(shadow->group);
        if (git == gstate.end()) {
            return; // 적용할 Group 정책이 없으면 건너뛴다.
        }
        domain::GroupRule const& rule = *git->second.rule;

        domain::DeviceThermalRegime& thermal = thermal_[id];
        domain::DeviceThermalRegime const previous = thermal;
        thermal = rule.next_thermal(thermal, shadow->status->temp);
        if (thermal != previous) {
            // 과열 진입과 해제를 모두 기록한다.
            LOG_POLICY_THERMAL_UPDATE(
                id.to_string(), domain::to_string(thermal), shadow->status->temp
            );
        }

        auto& commanded = commanded_[id];
        auto effective = rule.effective_mode(git->second.regime, thermal, commanded.mode);
        if (!effective && commanded.failed) {
            // 과열 해제 후 부하 상태가 미정이어도 실패한 복귀 명령의 목표를 다시 사용한다.
            effective = commanded.mode;
        }
        if (!effective) {
            return; // 아직 목표 모드를 결정할 수 없다.
        }
        if (commanded.mode == effective && !commanded.failed) {
            return; // 목표가 같고 실패하지 않은 명령은 다시 발행하지 않는다.
        }
        commanded.mode = effective;
        commanded.failed = false;
        pending_.emplace_back(id, *effective);
    });

    for (auto const& [device, mode] : pending_) {
        command_one(device, mode, now);
    }
}

void PolicyService::command_one(
    domain::DeviceId device, ddcs::device::Mode mode, common::Clock::time_point now
) {
    // 첫 전송에 실패하면 다음 정책 평가에서 다시 발행할 수 있도록 기록한다.
    // 모드를 전송할 바이트 값으로 변환할 때는 encode_mode()를 사용한다.
    auto const command = commands_.dispatch(
        device, wire::command::SetMode{.mode = ddcs::device::encode_mode(mode)}, now, this
    );
    // 전송 중 연결이 종료되어 명령 기록이 삭제되었다면 다시 만들지 않는다.
    if (auto const it = commanded_.find(device); it != commanded_.end()) {
        it->second.command = command;
        it->second.failed = !command.valid();
    }
}

} // namespace ddcs::ctrl::app::device
