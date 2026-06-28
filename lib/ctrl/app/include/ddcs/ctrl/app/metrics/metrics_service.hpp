#pragma once

#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/metrics/port/prometheus_source.hpp"
#include "ddcs/ctrl/app/metrics/sweep_stats.hpp"
#include "ddcs/ctrl/app/session/handshake_monitor.hpp"
#include "ddcs/ctrl/app/session/liveness_monitor.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"

#include <string>

namespace ddcs::ctrl::app::metrics {

// 메트릭 use-case: 현재 상태를 Prometheus text로 노출. gauge(현재값) + counter(누적/알람)를 pull
class MetricsService final : public port::PrometheusSource {
public:
    MetricsService(
        session::SessionRegistry const& sessions, domain::DeviceRegistry const& devices,
        device::CommandService const& commands, session::LivenessMonitor const& liveness,
        session::HandshakeMonitor const& handshake, domain::GroupPolicy const& policy,
        SweepStats const& sweep
    ) noexcept
        : sessions_(sessions),
          devices_(devices),
          commands_(commands),
          liveness_(liveness),
          handshake_(handshake),
          policy_(policy),
          sweep_(sweep) {}

    std::string scrape() override;

private:
    session::SessionRegistry const& sessions_;
    domain::DeviceRegistry const& devices_;
    device::CommandService const& commands_;
    session::LivenessMonitor const& liveness_;
    session::HandshakeMonitor const& handshake_;
    domain::GroupPolicy const& policy_; // per-group 메트릭을 정책 group으로 한정 (cardinality)
    SweepStats const& sweep_;           // sweep tick 작업 소요시간 통계
};

} // namespace ddcs::ctrl::app::metrics
