#include "ddcs/agent/agent_fleet.hpp"

#include "ddcs/agent/infra/transport/backoff_schedule.hpp"
#include "ddcs/agent/infra/transport/connector.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/signal_source.hpp"
#include "ddcs/io/throw_errno.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_scheduler.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <algorithm>
#include <csignal>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ddcs::agent {

class AgentFleet::Impl final : public io::TimerHandler {
    using Pool = common::ObjectPool<common::LinearBuffer>;

    struct Member {
        std::unique_ptr<domain::Device> device;
        infra::transport::Connector connector;
        app::session::SessionService session;

        Member(
            Agent::Config const& cfg, std::unique_ptr<domain::Device> dev, io::Reactor& reactor,
            io::TimerScheduler& timers, Pool& pool, std::uint64_t seed
        )
            : device(std::move(dev)),
              connector(
                  reactor, timers, cfg.controller_host, cfg.controller_port, cfg.rx_buffer_size,
                  infra::transport::BackoffSchedule{
                      cfg.reconnect_base_delay, cfg.reconnect_max_delay,
                      static_cast<std::uint32_t>(seed)
                  },
                  pool
              ),
              session(*device, connector, cfg.session) {
            connector.init(session);
        }
    };

public:
    Impl(Config cfg, std::vector<std::unique_ptr<domain::Device>> devices)
        : signal_source_(reactor_, {SIGINT, SIGTERM}, [this](int) { stop(); }),
          timers_(reactor_),
          pool_(Pool::create(wire::frame::max_frame_size)),
          count_(devices.size()),
          interval_(cfg.start_interval) {
        if (devices.empty() || interval_.count() < 0) {
            throw std::invalid_argument{"agent fleet: devices required and start interval >= 0"};
        }

        std::unordered_set<common::Uuid> ids;
        for (auto const& device : devices) {
            if (!device || device->id().is_nil() || !ids.insert(device->id()).second) {
                throw std::invalid_argument{"agent fleet: each device needs a unique non-nil UUID"};
            }
        }

        std::mt19937_64 rng{std::random_device{}()};
        members_.reserve(count_);
        for (auto& device : devices) {
            members_.push_back(
                std::make_unique<Member>(
                    cfg.agent, std::move(device), reactor_, timers_, pool_, rng()
                )
            );
        }
    }

    ~Impl() override {
        stop();
    }

    void start() {
        if (stopped_) {
            throw std::logic_error{"agent fleet: cannot restart after stop"};
        }

        if (started_) {
            return;
        }

        started_ = true;
        try {
            signal_source_.start();
            timers_.start();
            start_next();
        } catch (...) {
            stop();
            throw;
        }
    }

    void run() {
        if (!started_ || stopped_) {
            throw std::logic_error{"agent fleet: run requires a started fleet"};
        }

        reactor_.run();
    }

    void run_once(std::chrono::milliseconds timeout) {
        if (started_ && !stopped_) {
            reactor_.run_once(timeout);
        }
    }

    void stop() {
        if (stopped_) {
            return;
        }

        stopped_ = true;

        reactor_.stop();
        timers_.stop();
        timers_.cancel(start_timer_);
        members_.clear(); // Connector 타이머 취소와 버퍼 반환은 공유 자원 소멸 전에 끝낸다.
        signal_source_.stop();
    }

    std::size_t size() const noexcept {
        return count_;
    }

    std::size_t active_count() const noexcept {
        return static_cast<std::size_t>(
            std::count_if(members_.begin(), members_.end(), [](auto const& member) {
                return member->session.state() == app::session::SessionService::State::active;
            })
        );
    }

    void on_expired(io::TimerToken id) override {
        if (id == start_timer_) {
            start_timer_ = {};
            start_next();
        }
    }

private:
    void start_next() {
        do {
            if (auto const result = members_.at(next_++)->connector.start(); !result) {
                io::throw_boot_failure(result, "agent fleet transport start");
            }
        } while (interval_.count() == 0 && next_ < count_);

        if (next_ < count_) {
            start_timer_ = timers_.schedule(interval_, *this);
        }
    }

    io::Reactor reactor_;
    io::SignalSource signal_source_;
    io::TimerScheduler timers_;
    Pool pool_;
    std::vector<std::unique_ptr<Member>> members_;
    std::size_t count_;
    std::chrono::milliseconds interval_;
    io::TimerToken start_timer_;
    std::size_t next_ = 0;
    bool started_ = false;
    bool stopped_ = false;
};

AgentFleet::AgentFleet(Config cfg, std::vector<std::unique_ptr<domain::Device>> devices)
    : impl_(std::make_unique<Impl>(std::move(cfg), std::move(devices))) {}

AgentFleet::~AgentFleet() = default;

void AgentFleet::start() {
    impl_->start();
}

void AgentFleet::run() {
    impl_->run();
}

void AgentFleet::run_once(std::chrono::milliseconds timeout) {
    impl_->run_once(timeout);
}

void AgentFleet::stop() {
    impl_->stop();
}

std::size_t AgentFleet::size() const noexcept {
    return impl_->size();
}

std::size_t AgentFleet::active_count() const noexcept {
    return impl_->active_count();
}

} // namespace ddcs::agent
