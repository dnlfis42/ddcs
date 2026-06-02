#pragma once

#include "ddcs/agent/infra/backoff_schedule.hpp"
#include "ddcs/agent/infra/connection.hpp"
#include "ddcs/agent/port/inbound.hpp"
#include "ddcs/agent/port/outbound.hpp"
#include "ddcs/agent/port/timer_id.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <array>
#include <chrono>
#include <string>

#include <cstdint>

namespace ddcs::io {
class Reactor;
}

namespace ddcs::agent::infra {

using ddcs::agent::port::Inbound;
using ddcs::agent::port::Outbound;
using ddcs::agent::port::TimerId;

// agent 측 transport. 단일 connection 을 유지하고 끊기면 backoff 후 재연결.
// app 과는 Outbound(송신/타이머/close) / Inbound(통지) 포트로만 통신.
//  - Outbound 구현: payload_buffer/send/schedule_timer/cancel_timer/close
//  - io::TimerHandler: app 타이머 + reconnect 타이머 만료 수신
//  - Connection(io::IoHandler)의 on_io 위임을 on_connection_event 로 받음
// Reactor 는 이 클래스를 모른다. 재연결/framing 은 전부 여기 산다.
class Connector : public Outbound, public io::TimerHandler {
public:
    Connector(io::Reactor& reactor, std::string host, std::uint16_t port);
    ~Connector() override;

    Connector(Connector const&) = delete;
    Connector& operator=(Connector const&) = delete;
    Connector(Connector&&) noexcept = delete;
    Connector& operator=(Connector&&) noexcept = delete;

    void init(Inbound& handler) noexcept { handler_ = &handler; }
    void start(); // 첫 connect 시도

public: // Outbound (app -> transport)
    common::PoolHandle<common::LinearBuffer> payload_buffer() override;
    void send(std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) override;
    void schedule_timer(TimerId id, std::chrono::nanoseconds delay) override;
    void cancel_timer(TimerId id) override;
    void close() override;

public: // io::TimerHandler - Reactor 가 만료 통지
    void on_timer(io::TimerId id) override;

public: // Connection::on_io -> 위임
    void on_connection_event(Connection& conn, std::uint32_t events);

public: // 조회 (테스트)
    Connection::State state() const noexcept { return connection_.state(); }

private:
    void try_connect();
    void on_connecting(std::uint32_t events); // connecting: SO_ERROR 확인
    void on_connected_io(std::uint32_t events);
    void framing(); // rx 에서 완성 프레임 추출 -> Inbound::on_recv
    void update_interest();
    void disconnect_and_reconnect();
    void arm_reconnect();
    void cancel_app_timers();

    static constexpr std::size_t timer_slot_count{3}; // port::TimerId 종류 수
    static std::size_t slot_of(TimerId id) noexcept { return static_cast<std::size_t>(id); }

    io::Reactor& reactor_;
    std::string host_;
    std::uint16_t port_;
    Inbound* handler_{nullptr};
    // payload_pool_ 는 connection_ 보다 먼저 선언 - connection_ 의 tx_queue(PoolHandle)가
    // 역순 소멸에서 풀보다 *먼저* 반납되도록(풀이 핸들보다 오래 살아야 함). 순서 바꾸지 말 것.
    common::ObjectPool<common::LinearBuffer> payload_pool_;
    Connection connection_;
    BackoffSchedule backoff_;
    std::array<io::TimerId, timer_slot_count> app_timer_{}; // port::TimerId -> io::TimerId
    io::TimerId reconnect_timer_{};
};

} // namespace ddcs::agent::infra
