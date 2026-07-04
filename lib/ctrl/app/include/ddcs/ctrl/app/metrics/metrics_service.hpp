#pragma once

#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/device_roster.hpp"
#include "ddcs/ctrl/app/metrics/port/prometheus_source.hpp"
#include "ddcs/ctrl/app/metrics/sweep_stats.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/session/session_service.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"

#include <string>

namespace ddcs::ctrl::app::metrics {

// 메트릭 use-case: 현재 상태를 Prometheus text로 노출. gauge(현재값) + counter(누적/알람)를 pull
class MetricsService final : public port::PrometheusSource {
public:
    MetricsService(
        session::SessionRegistry const& sessions, domain::DeviceRegistry const& devices,
        device::port::DeviceRoster& roster, device::CommandService const& commands,
        session::SessionService const& session_service, domain::GroupPolicy const& policy,
        SweepStats const& sweep
    ) noexcept
        : sessions_(sessions),
          devices_(devices),
          roster_(roster),
          commands_(commands),
          session_service_(session_service),
          policy_(policy),
          sweep_(sweep) {}

    std::string scrape() override;

private:
    session::SessionRegistry const& sessions_;
    domain::DeviceRegistry const& devices_;
    device::port::DeviceRoster& roster_; // group 집계 입력. 정책 평가와 같은 active 집합
    device::CommandService const& commands_;
    session::SessionService const& session_service_; // 시한 감시 카운터(evict/expired) 출처
    domain::GroupPolicy const& policy_; // per-group 메트릭을 정책 group으로 한정 (cardinality)
    SweepStats const& sweep_;           // sweep tick 작업 소요시간 통계
};

} // namespace ddcs::ctrl::app::metrics
