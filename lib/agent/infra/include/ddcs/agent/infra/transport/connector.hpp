#pragma once

#include "ddcs/agent/app/transport/port/inbound.hpp"
#include "ddcs/agent/app/transport/port/outbound.hpp"
#include "ddcs/agent/app/transport/port/timer_slot.hpp"
#include "ddcs/agent/infra/transport/backoff_schedule.hpp"
#include "ddcs/agent/infra/transport/connection.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace ddcs::io {

class Reactor;
class TimerScheduler;

} // namespace ddcs::io

namespace ddcs::agent::infra::transport {

namespace port = ddcs::agent::app::transport::port;

// agent 측 transport
// - 단일 connection을 유지하고 끊기면 backoff 후 재연결
// - app과는 Outbound(송신/타이머/close) / Inbound(통지) 포트로만 통신
// - io::TimerHandler: app 타이머 + reconnect 타이머 만료 수신
// - Connection(io::ChannelHandler)의 on_ready 위임을 on_connection_event로 받음
class Connector : public port::Outbound, public io::TimerHandler {
public:
    Connector(
        io::Reactor& reactor, io::TimerScheduler& timers, std::string host, std::uint16_t port,
        std::chrono::nanoseconds reconnect_base = BackoffSchedule::default_base_delay,
        std::chrono::nanoseconds reconnect_max = BackoffSchedule::default_max_delay
    );
    ~Connector() override;

    Connector(Connector const&) = delete;
    Connector& operator=(Connector const&) = delete;
    Connector(Connector&&) noexcept = delete;
    Connector& operator=(Connector&&) noexcept = delete;

    common::PoolHandle<common::LinearBuffer> payload_buffer() override;
    void send(common::PoolHandle<common::LinearBuffer> message) override;
    void schedule_timer(port::TimerSlot id, std::chrono::nanoseconds delay) override;
    void cancel_timer(port::TimerSlot id) override;
    void close() override;
    void notify_registered() override;

    void on_expired(io::TimerId id) override;

    void init(port::Inbound& handler) noexcept {
        handler_ = &handler;
    }

    // 첫 connect 시도
    void start();

    Connection::State state() const noexcept {
        return connection_.state();
    }

    void on_connection_event(Connection& conn, io::ChannelEvents events);

private:
    static constexpr std::size_t timer_slot_count = 3;

    static std::size_t slot_of(port::TimerSlot id) noexcept {
        return static_cast<std::size_t>(id);
    }

    void try_connect();
    void on_connecting(io::ChannelEvents events); // connecting: SO_ERROR 확인
    void on_connected_io(io::ChannelEvents events);
    void framing(); // rx에서 완성 프레임 추출 후 Inbound::on_recv
    void update_interest();
    void disconnect_and_reconnect();
    void arm_reconnect();
    void cancel_app_timers();

    io::Reactor& reactor_;
    io::TimerScheduler& timers_;
    std::string host_;
    std::uint16_t port_;
    port::Inbound* handler_ = nullptr;
    // payload_pool_는 connection_보다 먼저 선언한다.
    // connection_의 tx_queue(PoolHandle)가 역순 소멸에서 풀보다 먼저 반납되도록 해야한다.
    // 풀이 핸들보다 오래 살아야 함
    common::ObjectPool<common::LinearBuffer> payload_pool_;
    Connection connection_;
    BackoffSchedule backoff_;
    std::array<io::TimerId, timer_slot_count> app_timer_; // port::TimerSlot을 io::TimerId로 매핑
    io::TimerId reconnect_timer_;
};

} // namespace ddcs::agent::infra::transport
