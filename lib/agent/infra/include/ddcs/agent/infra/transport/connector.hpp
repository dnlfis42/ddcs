#pragma once

#include "ddcs/io/sys_result.hpp"

#include "ddcs/agent/app/transport/port/inbound.hpp"
#include "ddcs/agent/app/transport/port/message_buffer.hpp"
#include "ddcs/agent/app/transport/port/outbound.hpp"
#include "ddcs/agent/app/transport/port/timer_slot.hpp"
#include "ddcs/agent/infra/transport/backoff_schedule.hpp"
#include "ddcs/agent/infra/transport/connection.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_token.hpp"

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

// agent 측 transport. 단일 connection을 유지하고 끊기면 backoff 후 재연결.
// app과는 Outbound(구현) / Inbound(통지) 포트로만 통신한다.
class Connector : public port::Outbound, public io::TimerHandler {
public:
    // rx 값의 단일 출처는 Agent::Config다. 기본값 없이 명시 전달만 받는다.
    // backoff는 조립 루트가 seed까지 채워 완성해 전달한다.
    Connector(
        io::Reactor& reactor, io::TimerScheduler& timer_scheduler, std::string host,
        std::uint16_t port, std::size_t rx_buffer_size, BackoffSchedule backoff
    );
    ~Connector() override;

    Connector(Connector const&) = delete;
    Connector& operator=(Connector const&) = delete;
    Connector(Connector&&) noexcept = delete;
    Connector& operator=(Connector&&) noexcept = delete;

    void notify_registered() override;
    void disconnect(port::DisconnectReason reason) override;

    port::MessageBuffer make_message_buffer() override;
    void send(port::MessageBuffer message) override;

    void schedule_timer(port::TimerSlot id, std::chrono::nanoseconds delay) override;
    void cancel_timer(port::TimerSlot id) override;

    void on_expired(io::TimerToken id) override; // app 타이머 + reconnect 타이머 만료 수신

    void init(port::Inbound& handler) noexcept {
        handler_ = &handler;
    }

    // 첫 connect 시도
    // init() 전에 부르면 errno 없는 실패를 돌려준다. 조립 루트가 예외로 바꾼다.
    [[nodiscard]] io::SysResult start();

    Connection::State state() const noexcept {
        return connection_.state();
    }

    // Connection(io::ChannelHandler)의 on_ready 위임 수신
    void on_connection_event(Connection& conn, io::ChannelEvents events);

private:
    void connect();

    void handle_connecting(io::ChannelEvents events); // connecting: SO_ERROR 확인
    void handle_connected(io::ChannelEvents events);

    void update_interests();

    void disconnect_and_reconnect(port::DisconnectReason reason);
    void schedule_reconnect();

    io::Reactor& reactor_;
    io::TimerScheduler& timer_scheduler_;

    std::string host_;
    std::uint16_t port_;

    port::Inbound* handler_ = nullptr;

    common::ObjectPool<common::LinearBuffer> message_pool_;

    Connection connection_;

    BackoffSchedule backoff_;

    io::TimerToken reconnect_timer_;
    std::array<io::TimerToken, port::timer_slot_count> app_timer_;

    // 주소 해석 실패는 풀릴 때까지 재연결마다 되풀이되므로, 들어갈 때 한 번만 알리고
    // 그 동안 헛돈 시도를 세었다가 풀릴 때 함께 알린다. Acceptor의 fd 고갈 래치와 같은 모양이다.
    bool host_unresolved_ = false;
    std::uint64_t unresolved_attempts_ = 0;
};

} // namespace ddcs::agent::infra::transport
