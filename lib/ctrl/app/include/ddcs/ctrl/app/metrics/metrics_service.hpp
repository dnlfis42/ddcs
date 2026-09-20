#pragma once

#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/active_devices.hpp"
#include "ddcs/ctrl/app/metrics/port/prometheus_source.hpp"
#include "ddcs/ctrl/app/metrics/tick_stats.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/session/session_service.hpp"
#include "ddcs/ctrl/app/transport/port/transport_stats.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"

#include <string>

namespace ddcs::ctrl::app::metrics {

// 현재 상태와 누적 통계를 Prometheus 텍스트 형식으로 제공한다.
class MetricsService final : public port::PrometheusSource {
public:
    MetricsService(
        session::SessionRegistry const& sessions, domain::DeviceRegistry const& devices,
        device::port::ActiveDevices& active_devices, device::CommandService const& commands,
        session::SessionService const& session_service, domain::GroupPolicy const& policy,
        TickStats const& sweep, transport::port::TransportStatsSource const& transport_stats
    ) noexcept
        : sessions_(sessions),
          devices_(devices),
          active_devices_(active_devices),
          commands_(commands),
          session_service_(session_service),
          policy_(policy),
          sweep_(sweep),
          transport_stats_(transport_stats) {}

    std::string scrape() override;

private:
    session::SessionRegistry const& sessions_;
    domain::DeviceRegistry const& devices_;
    device::port::ActiveDevices& active_devices_; // 정책 평가와 동일한 활성 Device를 Group별로 집계한다.
    device::CommandService const& commands_;
    session::SessionService const& session_service_; // 수신 메시지와 연결 종료 횟수를 제공한다.
    domain::GroupPolicy const&
        policy_;             // 정책에 정의된 Group만 메트릭으로 제공한다.
    TickStats const& sweep_; // Controller tick 작업 시간, 시작 지연, 건너뛴 횟수
    transport::port::TransportStatsSource const& transport_stats_; // 송신 큐 깊이·풀 사용률
};

} // namespace ddcs::ctrl::app::metrics
