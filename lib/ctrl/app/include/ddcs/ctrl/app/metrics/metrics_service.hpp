#pragma once

#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/session/liveness_monitor.hpp"
#include "ddcs/ctrl/app/session/session_manager.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/ctrl/port/metrics/inbound.hpp"

#include <string>

namespace ddcs::ctrl::app::metrics {

using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::app::session::LivenessMonitor;
using ddcs::ctrl::app::session::SessionManager;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::agent::AgentRegistry;

// 메트릭 use-case: 현재 상태를 Prometheus text로 노출. gauge(현재값) + counter(누적/알람)를 pull.
class MetricsService final : public ddcs::ctrl::port::metrics::Inbound {
public:
    MetricsService(
        SessionRegistry const& sessions, AgentRegistry const& registry, CommandService const& commands,
        SessionManager const& session_manager, LivenessMonitor const& liveness
    ) noexcept
        : sessions_{sessions}, registry_{registry}, commands_{commands}, session_manager_{session_manager},
          liveness_{liveness} {}

    std::string scrape() override;

private:
    SessionRegistry const& sessions_;
    AgentRegistry const& registry_;
    CommandService const& commands_;
    SessionManager const& session_manager_;
    LivenessMonitor const& liveness_;
};

} // namespace ddcs::ctrl::app::metrics
