#include "ddcs/agent/agent.hpp"

#include <csignal>
#include <utility>

namespace ddcs::agent {

Agent::Agent(Config cfg)
    : device_{std::move(cfg.device)}, signal_source_{reactor_, {SIGINT, SIGTERM}, [this] { stop(); }},
      connector_{reactor_, cfg.controller_host, cfg.controller_port},
      session_{cfg.agent_uuid, *device_, connector_, cfg.session} {
    auto& lg = logger::Logger::instance();
    lg.set_level(cfg.log_level);
    lg.set_sink(cfg.log_sink != nullptr ? *cfg.log_sink : default_sink_);

    if (cfg.agent_uuid_is_ephemeral) {
        LOG_WARN("agent.uuid_ephemeral", ddcs::logger::kv("uuid", cfg.agent_uuid.to_string()));
    }
    connector_.init(session_); // inbound 포트 주입
}

Agent::~Agent() {
    stop();
    // 멤버 dtor 역순: session_ -> connector_ -> signal_source_ -> reactor_ -> device_.
}

void Agent::start() {
    signal_source_.start();
    connector_.start();
}

void Agent::run() { reactor_.run(); }

void Agent::run_once(std::chrono::milliseconds timeout) { reactor_.run_once(timeout); }

void Agent::stop() {
    signal_source_.stop();
    reactor_.stop();
}

} // namespace ddcs::agent
