#pragma once

#include "ddcs/ctrl/app/ops/operator_service.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/json/value.hpp"

#include <optional>
#include <string>
#include <unordered_map>

#include <cstdint>

namespace ddcs::ctrl::app::policy {

using ddcs::ctrl::app::ops::OperatorService;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::DeviceRegistry;

// policy.json ({"groups":{<name>:{high_load,low_load,busy_mode,idle_mode}}}) -> GroupPolicy.
// 필드 누락/타입오류/미지 mode -> nullopt. load(read+parse)와 apply(set_policy) 분리 - 핫리로드(future) 대비.
std::optional<domain::GroupPolicy> parse_policy(json::Value const& root);

// 정책 엔진: 주기 evaluate 로 활성 agent 의 그룹별 평균 load 집계 -> 히스테리시스 임계 비교 ->
// regime(busy/idle) 전환 시에만 그룹의 활성 agent 들에게 SetMode 발신(전환마다 1회 - 스팸 없음, 복귀 지원).
class PolicyService {
private:
    enum class Regime : std::uint8_t { unknown, busy, idle };

public:
    PolicyService(SessionRegistry& sessions, DeviceRegistry& registry, OperatorService& ops) noexcept
        : sessions_{sessions}, registry_{registry}, ops_{ops} {}

    void set_policy(domain::GroupPolicy policy); // load-once/핫리로드 apply (regime 리셋)
    void evaluate();                             // 주기 호출(Controller sweep tick)

private:
    void command_group(std::string const& group, device::Mode mode); // 그룹 활성 agent에 SetMode

private:
    SessionRegistry& sessions_;
    DeviceRegistry& registry_;
    OperatorService& ops_;
    domain::GroupPolicy policy_;
    std::unordered_map<std::string, Regime> regime_; // group -> 현재 regime(전환 감지)
};

} // namespace ddcs::ctrl::app::policy
