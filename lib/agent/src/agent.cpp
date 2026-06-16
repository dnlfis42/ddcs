#include "ddcs/agent/agent.hpp"

#include "ddcs/agent/infra/frame/connector.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/signal_source.hpp"
#include "ddcs/io/timer_scheduler.hpp"

#include <csignal>
#include <memory>
#include <utility>

namespace ddcs::agent {

class Agent::Impl final {
public:
    explicit Impl(Config cfg);
    ~Impl();

    Impl(Impl const&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void start();
    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop();

    app::SessionService& session() noexcept { return session_; }

private:
    logger::StdoutSink default_sink_;
    std::unique_ptr<domain::Device> device_;
    io::Reactor reactor_;
    io::SignalSource signal_source_;
    io::TimerScheduler timer_scheduler_;
    infra::frame::Connector connector_;
    app::SessionService session_;
};

Agent::Impl::Impl(Config cfg)
    : device_{std::move(cfg.device)}, signal_source_{reactor_, {SIGINT, SIGTERM}, [this](int) { stop(); }},
      timer_scheduler_{reactor_}, connector_{reactor_, timer_scheduler_, cfg.controller_host, cfg.controller_port},
      session_{cfg.agent_uuid, *device_, connector_, cfg.session} {
    auto& lg = logger::Logger::instance();
    lg.set_level(cfg.log_level);
    lg.set_sink(cfg.log_sink != nullptr ? *cfg.log_sink : default_sink_);

    if (cfg.agent_uuid_is_ephemeral) {
        LOG_WARN("agent.uuid_ephemeral", ddcs::logger::kv("uuid", cfg.agent_uuid.to_string()));
    }
    connector_.init(session_); // inbound 포트 주입
}

Agent::Impl::~Impl() {
    stop();
    // 멤버 dtor 역순: session_, connector_, timer_scheduler_, signal_source_, reactor_, device_ 순서로.
}

void Agent::Impl::start() {
    signal_source_.start();
    timer_scheduler_.start();
    connector_.start();
}

void Agent::Impl::run() { reactor_.run(); }

void Agent::Impl::run_once(std::chrono::milliseconds timeout) { reactor_.run_once(timeout); }

void Agent::Impl::stop() {
    timer_scheduler_.stop();
    signal_source_.stop();
    reactor_.stop();
}

Agent::Agent(Config cfg) : impl_{std::make_unique<Impl>(std::move(cfg))} {}

Agent::~Agent() = default;

void Agent::start() { impl_->start(); }

void Agent::run() { impl_->run(); }

void Agent::run_once(std::chrono::milliseconds timeout) { impl_->run_once(timeout); }

void Agent::stop() { impl_->stop(); }

app::SessionService& Agent::session() noexcept { return impl_->session(); }

} // namespace ddcs::agent
