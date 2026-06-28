#include "ddcs/ctrl/app/device/policy_service.hpp"

#include "ddcs/ctrl/domain/device_shadow.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/message/command.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ddcs::ctrl::app::device {

namespace msg = ddcs::wire::message;

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
        auto rule = domain::GroupRule::try_make(*hv, *lv, *bm, *im, thermal);
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
    // 새 정책으로 재명령하도록 발신 belief만 비운다. regime/thermal은 히스테리시스 latch라
    // 핫리로드(SIGHUP)를 넘어 보존한다: thermal을 비우면 과열 latch가 resume_temp 전에 조기
    // 해제되고, regime을 비우면 데드밴드(low<avg<high) group이 새 룰의 mode를 재적용하지 못한다.
    commanded_.clear();
}

void PolicyService::on_device_left(domain::DeviceId device) {
    // 세션이 끝난 device의 per-device 제어 belief를 폐기한다. 같은 id로 재접속(=normal로
    // 리부트)하면 stale한 commanded_ 때문에 재명령이 suppress되는 일을 막고(재명령 보장), 맵 증식도
    // 막는다.
    commanded_.erase(device);
    thermal_.erase(device);
}

void PolicyService::evaluate(common::Clock::time_point now) {
    if (policy_.empty()) {
        return;
    }

    // 1. group별 평균 load 집계 (load 정책은 그룹 단위; 끊긴 device의 stale Shadow 제외)
    struct Agg {
        double load_sum{};
        int count{};
    };
    std::unordered_map<std::string, Agg> agg;
    roster_.for_each_active([&](domain::DeviceId id) {
        auto const* shadow = devices_.find(id);
        if (shadow == nullptr || shadow->group.empty()) {
            return;
        }
        auto& a = agg[shadow->group];
        a.load_sum += shadow->status.load;
        ++a.count;
    });

    // 2. group별 load regime -> base mode. regime 전환은 group 단위로 로그(1회).
    struct GroupState {
        domain::GroupRule const* rule;
        std::optional<ddcs::device::Mode> base_mode; // regime 미확정이면 nullopt
    };
    std::unordered_map<std::string, GroupState> gstate;
    policy_.for_each([&](std::string const& group, domain::GroupRule const& rule) {
        auto const it = agg.find(group);
        if (it == agg.end() || it->second.count == 0) {
            return; // active device 없는 group은 skip
        }
        double const avg = it->second.load_sum / static_cast<double>(it->second.count);
        Regime& regime = regime_[group];
        Regime const previous = regime;
        if (avg > rule.high_load()) {
            regime = Regime::busy;
        } else if (avg < rule.low_load()) {
            regime = Regime::idle;
        }
        if (regime != previous) {
            if (regime == Regime::busy) {
                LOG_WARN("policy.busy", logger::kv("group", group), logger::kv("avg_load", avg));
            } else if (regime == Regime::idle) {
                LOG_INFO("policy.idle", logger::kv("group", group), logger::kv("avg_load", avg));
            }
        }
        std::optional<ddcs::device::Mode> base;
        if (regime == Regime::busy) {
            base = rule.high_load_mode();
        } else if (regime == Regime::idle) {
            base = rule.low_load_mode();
        }
        gstate.emplace(group, GroupState{&rule, base});
    });

    // 3. device별 thermal override 합성. 각 device가 자기 온도로 트립/해제하고, 뜨거운 device만
    //    high_temp_mode로 가고 나머지는 group base mode. effective가 바뀐 device만 명령한다.
    //    (dispatch가 순회 중 disconnect를 부를 수 있어 대상을 모은 뒤 순회 밖에서 발송)
    pending_.clear();
    roster_.for_each_active([&](domain::DeviceId id) {
        auto const* shadow = devices_.find(id);
        if (shadow == nullptr) {
            return;
        }
        auto const git = gstate.find(shadow->group);
        if (git == gstate.end()) {
            return; // 정책 없는/비활성 group
        }
        domain::GroupRule const& rule = *git->second.rule;

        // 이 device의 thermal 히스테리시스 (자기 온도 기준, resume~high 데드밴드)
        Thermal& thermal = thermal_[id];
        Thermal const previous = thermal;
        if (rule.thermal()) {
            if (shadow->status.temp > rule.thermal()->high_temp) {
                thermal = Thermal::hot;
            } else if (shadow->status.temp < rule.thermal()->resume_temp) {
                thermal = Thermal::cool;
            }
        } else {
            // 룰에 thermal 없음(또는 reload로 제거됨): latch 해제 -> load 제어로 복귀.
            // 아래 high_temp_mode deref는 thermal==hot일 때만 일어나므로 이로써 항상 안전하다.
            thermal = Thermal::cool;
        }
        if (thermal == Thermal::hot && previous != Thermal::hot) {
            LOG_WARN(
                "policy.hot", logger::kv("device", id.to_string()),
                logger::kv("temp", shadow->status.temp)
            );
        }

        // effective: thermal hot이면 그 device만 high_temp_mode, 아니면 group base mode
        std::optional<ddcs::device::Mode> effective;
        if (thermal == Thermal::hot) {
            effective = rule.thermal()->high_temp_mode;
        } else if (git->second.base_mode) {
            effective = *git->second.base_mode;
        } else if (rule.thermal() && commanded_[id] == rule.thermal()->high_temp_mode) {
            // thermal 해제됐는데 group regime 미확정: 비상모드 latch 풀어 baseline 복귀
            effective = rule.low_load_mode();
        }
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
    auto buf = commands_.make_command_buffer();
    auto const written =
        msg::encode_set_mode(buf->tailroom_span(), ddcs::device::encode_mode(mode));
    if (!written || !buf->try_commit(*written)) {
        LOG_ERROR("policy.encode_fail", logger::kv("device", device.to_string())); // 버그 신호
        return;
    }
    // 미연결 등 송신 실패는 dispatch가 invalid 반환 + WARN. 다음 평가가 자연 보상.
    commands_.dispatch(
        device, static_cast<std::uint8_t>(msg::CommandType::set_mode), std::move(buf), now
    );
}

} // namespace ddcs::ctrl::app::device
