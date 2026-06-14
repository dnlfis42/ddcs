#pragma once

#include "ddcs/ctrl/app/agent/agent_registry.hpp"
#include "ddcs/ctrl/app/agent/handshake_monitor.hpp"
#include "ddcs/ctrl/app/agent/liveness_monitor.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/metrics/port/metrics_source.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"

#include <string>

namespace ddcs::ctrl::app::metrics {

// 메트릭 use-case: 현재 상태를 Prometheus text로 노출. gauge(현재값) + counter(누적/알람)를 pull.
class MetricsService final : public port::MetricsSource {
public:
    MetricsService(
        agent::AgentRegistry const& agents, domain::DeviceRegistry const& devices,
        device::CommandService const& commands, agent::LivenessMonitor const& liveness,
        agent::HandshakeMonitor const& handshake
    ) noexcept
        : agents_{agents}, devices_{devices}, commands_{commands}, liveness_{liveness}, handshake_{handshake} {}

    std::string scrape() override;

private:
    agent::AgentRegistry const& agents_;
    domain::DeviceRegistry const& devices_;
    device::CommandService const& commands_;
    agent::LivenessMonitor const& liveness_;
    agent::HandshakeMonitor const& handshake_;
};

} // namespace ddcs::ctrl::app::metrics
