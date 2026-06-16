#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/device_roster.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/json/value.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ddcs::ctrl::app::device {

// policy.json ({"groups":{<name>:{high_load,low_load,busy_mode,idle_mode}}})을 GroupPolicy로 변환
// 필드 누락/타입오류/미지 mode면 nullopt.
// load(read+parse)와 apply(set_policy) 분리는 핫리로드(future) 대비
std::optional<domain::GroupPolicy> parse_policy(json::Value const& root);

// 정책 엔진: 주기 evaluate로 active device의 그룹별 평균 load 집계 후 히스테리시스 임계 비교를 거쳐
//            regime(busy/idle) 전환 시에만 그룹의 active device들에게 SetMode 발신
//            (전환마다 1회, 스팸 없음, 복귀 지원)
//  전달 신뢰성(supersede/동일 id 재전송)은 CommandService 몫
class PolicyService {
private:
    enum class Regime : std::uint8_t { unknown, busy, idle };

public:
    PolicyService(
        port::DeviceRoster& roster, domain::DeviceRegistry& devices, CommandService& commands
    ) noexcept
        : roster_{roster},
          devices_{devices},
          commands_{commands} {}

    void set_policy(domain::GroupPolicy policy);  // load-once/핫리로드 apply (regime 리셋)
    void evaluate(common::Clock::time_point now); // 주기 호출(조립 루트 tick)

private:
    void
    command_group(std::string const& group, ddcs::device::Mode mode, common::Clock::time_point now);

private:
    port::DeviceRoster& roster_;
    domain::DeviceRegistry& devices_;
    CommandService& commands_;
    domain::GroupPolicy policy_;
    std::unordered_map<std::string, Regime> regime_; // group을 현재 regime으로 매핑(전환 감지)
    std::vector<domain::DeviceId> targets_;          // PERF: command_group 발송 대상 재사용 버퍼
};

} // namespace ddcs::ctrl::app::device
