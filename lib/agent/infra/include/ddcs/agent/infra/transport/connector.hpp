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

// Controller와의 연결을 유지하고, 끊기면 재시도 간격에 따라 다시 연결한다.
// Outbound로 요청을 받고 Inbound로 연결 상태와 수신 메시지를 전달한다.
class Connector : public port::Outbound, public io::TimerHandler {
public:
    // 수신 버퍼 크기는 Agent::Config에서, 재연결 설정은 난수 시드까지 지정해 전달한다.
    Connector(
        io::Reactor& reactor, io::TimerScheduler& timer_scheduler, std::string host,
        std::uint16_t port, std::size_t rx_buffer_size, BackoffSchedule backoff
    );
    // 공유 풀은 Connector와 송수신 버퍼보다 오래 살아야 한다. 같은 reactor에서만 사용한다.
    Connector(
        io::Reactor& reactor, io::TimerScheduler& timer_scheduler, std::string host,
        std::uint16_t port, std::size_t rx_buffer_size, BackoffSchedule backoff,
        common::ObjectPool<common::LinearBuffer>& message_pool
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

    void on_expired(io::TimerToken id) override; // Agent 타이머와 재연결 타이머 처리

    void init(port::Inbound& handler) noexcept {
        handler_ = &handler;
    }

    // 연결을 시작한다. 재연결 대기 중이면 즉시 재시도한다.
    // init() 전에 호출하면 오류 번호 없이 실패를 반환한다.
    [[nodiscard]] io::SysResult start();

    Connection::State state() const noexcept {
        return connection_.state();
    }

    // Connection에서 전달한 소켓 이벤트를 처리한다.
    void on_connection_event(Connection& conn, io::ChannelEvents events);

private:
    void connect();

    void handle_connecting(io::ChannelEvents events); // SO_ERROR로 연결 성공 여부 확인
    void handle_connected(io::ChannelEvents events);

    void update_interests();

    void disconnect_and_reconnect(port::DisconnectReason reason);
    void schedule_reconnect();

    io::Reactor& reactor_;
    io::TimerScheduler& timer_scheduler_;

    std::string host_;
    std::uint16_t port_;

    port::Inbound* handler_ = nullptr;

    common::ObjectPool<common::LinearBuffer> owned_message_pool_;
    common::ObjectPool<common::LinearBuffer>& message_pool_;

    Connection connection_;

    BackoffSchedule backoff_;

    io::TimerToken reconnect_timer_;
    std::array<io::TimerToken, port::timer_slot_count> app_timer_;

    // 주소 해석 실패는 처음 한 번만 기록하고, 복구되면 누적 실패 횟수를 기록한다.
    bool host_unresolved_ = false;
    std::uint64_t unresolved_attempts_ = 0;
};

} // namespace ddcs::agent::infra::transport
