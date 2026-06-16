#pragma once

#include "ddcs/agent/app/port/inbound.hpp"
#include "ddcs/agent/app/port/outbound.hpp"
#include "ddcs/agent/app/port/timer_id.hpp"
#include "ddcs/agent/infra/backoff_schedule.hpp"
#include "ddcs/agent/infra/frame/connection.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <array>
#include <chrono>
#include <string>

#include <cstdint>

namespace ddcs::io {
class Reactor;
class TimerScheduler;
} // namespace ddcs::io

namespace ddcs::agent::infra::frame {

using ddcs::agent::app::port::Inbound;
using ddcs::agent::app::port::Outbound;
using ddcs::agent::app::port::TimerId;

// agent 측 transport. 단일 connection을 유지하고 끊기면 backoff 후 재연결.
// app과는 Outbound(송신/타이머/close) / Inbound(통지) 포트로만 통신.
// - Outbound 구현: payload_buffer/send/schedule_timer/cancel_timer/close
// - io::TimerHandler: app 타이머 + reconnect 타이머 만료 수신
// - Connection(io::ChannelHandler)의 on_ready 위임을 on_connection_event로 받음
// Reactor는 이 클래스를 모른다. 재연결/framing은 전부 여기 산다.
class Connector : public Outbound, public io::TimerHandler {
public:
    Connector(io::Reactor& reactor, io::TimerScheduler& timers, std::string host, std::uint16_t port);
    ~Connector() override;

    Connector(Connector const&) = delete;
    Connector& operator=(Connector const&) = delete;
    Connector(Connector&&) noexcept = delete;
    Connector& operator=(Connector&&) noexcept = delete;

    void init(Inbound& handler) noexcept { handler_ = &handler; }
    void start(); // 첫 connect 시도

public: // Outbound (app에서 transport로)
    common::PoolHandle<common::LinearBuffer> payload_buffer() override;
    void send(common::PoolHandle<common::LinearBuffer> message) override;
    void schedule_timer(TimerId id, std::chrono::nanoseconds delay) override;
    void cancel_timer(TimerId id) override;
    void close() override;

public: // io::TimerHandler: TimerScheduler가 만료 통지
    void on_expired(io::TimerId id) override;

public: // Connection::on_ready 위임 받음
    void on_connection_event(Connection& conn, io::ChannelEvents events);

public: // 조회 (테스트)
    Connection::State state() const noexcept { return connection_.state(); }

private:
    void try_connect();
    void on_connecting(io::ChannelEvents events); // connecting: SO_ERROR 확인
    void on_connected_io(io::ChannelEvents events);
    void framing(); // rx에서 완성 프레임 추출 후 Inbound::on_recv
    void update_interest();
    void disconnect_and_reconnect();
    void arm_reconnect();
    void cancel_app_timers();

    static constexpr std::size_t timer_slot_count{3}; // port::TimerId 종류 수
    static std::size_t slot_of(TimerId id) noexcept { return static_cast<std::size_t>(id); }

    io::Reactor& reactor_;
    io::TimerScheduler& timers_;
    std::string host_;
    std::uint16_t port_;
    Inbound* handler_{nullptr};
    // payload_pool_는 connection_보다 먼저 선언한다. connection_의 tx_queue(PoolHandle)가
    // 역순 소멸에서 풀보다 *먼저* 반납되도록(풀이 핸들보다 오래 살아야 함). 순서 바꾸지 말 것.
    common::ObjectPool<common::LinearBuffer> payload_pool_;
    Connection connection_;
    BackoffSchedule backoff_;
    std::array<io::TimerId, timer_slot_count> app_timer_{}; // port::TimerId를 io::TimerId로 매핑
    io::TimerId reconnect_timer_{};
};

} // namespace ddcs::agent::infra::frame
