#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/device_release_sink.hpp"
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
#include <utility>
#include <vector>

namespace ddcs::ctrl::app::device {

// policy ({"groups":{<name>:{high_load,low_load,high_load_mode,low_load_mode}}})을 GroupPolicy로
// 변환 필드 누락/타입오류/미지 mode면 nullopt. load(read+parse)와 apply(set_policy) 분리는
// 핫리로드(future) 대비
std::optional<domain::GroupPolicy> parse_policy(json::Value const& root);

// 정책 엔진
// - load: 그룹 단위. active device 평균 load를 히스테리시스로 봐서 group base mode(busy/idle)를
// 정함.
// - thermal: device 단위. 각 device가 자기 온도로 high_temp 트립 / resume_temp 해제 -- 뜨거운
//   device만 high_temp_mode로 가고 나머지는 group base mode(thermal이 load를 이김).
// - device별 effective mode(group base + 자기 thermal)가 바뀔 때만 그 device에 SetMode (스팸 없음).
// - 전달 신뢰성(supersede/동일 id 재전송)은 CommandService 몫
// - device 세션 종료(DeviceReleaseSink)시 그 device의 per-device 제어 상태를 폐기한다.
class PolicyService : public port::DeviceReleaseSink {
private:
    enum class Regime : std::uint8_t { unknown, busy, idle };
    enum class Thermal : std::uint8_t { cool, hot };

public:
    PolicyService(
        port::DeviceRoster& roster, domain::DeviceRegistry& devices, CommandService& commands
    ) noexcept
        : roster_{roster},
          devices_{devices},
          commands_{commands} {}

    void set_policy(domain::GroupPolicy policy);  // load-once/핫리로드 apply (regime 리셋)
    void evaluate(common::Clock::time_point now); // 주기 호출(조립 루트 tick)

    // DeviceReleaseSink: device 세션 종료 시 그 device의 제어 belief(commanded/thermal) 폐기.
    void on_device_left(domain::DeviceId device) override;

    // 로드된 정책 (알려진 그룹 집합)
    // - set_policy가 같은 멤버를 in-place 교체해도 참조는 계속 유효하다.
    [[nodiscard]] domain::GroupPolicy const& policy() const noexcept {
        return policy_;
    }

private:
    void
    command_one(domain::DeviceId device, ddcs::device::Mode mode, common::Clock::time_point now);

private:
    port::DeviceRoster& roster_;
    domain::DeviceRegistry& devices_;
    CommandService& commands_;
    domain::GroupPolicy policy_;
    std::unordered_map<std::string, Regime> regime_; // group의 load regime (load는 그룹 단위)
    std::unordered_map<domain::DeviceId, Thermal>
        thermal_; // device별 thermal 상태 (per-device 트립)
    // device별 마지막 발신 effective mode (group base + 자기 thermal). 변할 때만 발신.
    std::unordered_map<domain::DeviceId, std::optional<ddcs::device::Mode>> commanded_;
    std::vector<std::pair<domain::DeviceId, ddcs::device::Mode>> pending_; // 순회 밖 발송 버퍼
};

} // namespace ddcs::ctrl::app::device
