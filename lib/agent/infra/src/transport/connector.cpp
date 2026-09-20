#include "ddcs/agent/infra/transport/connector.hpp"

#include "ddcs/agent/app/transport/port/message_buffer.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_scheduler.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace ddcs::agent::infra::transport {

namespace {

constexpr std::size_t pool_chunk = 64;

// 쓰기 가능 이벤트로 비동기 연결 완료를 확인한다.
constexpr io::ChannelEvents connect_interest{
    io::ChannelEvents::writable | io::ChannelEvents::edge_triggered
};

constexpr io::ChannelEvents read_interest{
    io::ChannelEvents::readable | io::ChannelEvents::edge_triggered
};

} // namespace

Connector::Connector(
    io::Reactor& reactor, io::TimerScheduler& timer_scheduler, std::string host, std::uint16_t port,
    std::size_t rx_buffer_size, BackoffSchedule backoff
)
    : reactor_(reactor),
      timer_scheduler_(timer_scheduler),
      host_(std::move(host)),
      port_(port),
      owned_message_pool_(
          common::ObjectPool<common::LinearBuffer>::create<pool_chunk>(wire::frame::max_frame_size)
      ),
      message_pool_(owned_message_pool_),
      connection_(wire::frame::fit_rx_capacity(rx_buffer_size)),
      backoff_(backoff) {
    if (auto const fitted = wire::frame::fit_rx_capacity(rx_buffer_size);
        fitted != rx_buffer_size) {
        LOG_TRANSPORT_RX_BUFFER_ADJUST(rx_buffer_size, fitted);
    }
}

Connector::Connector(
    io::Reactor& reactor, io::TimerScheduler& timer_scheduler, std::string host, std::uint16_t port,
    std::size_t rx_buffer_size, BackoffSchedule backoff,
    common::ObjectPool<common::LinearBuffer>& message_pool
)
    : reactor_(reactor),
      timer_scheduler_(timer_scheduler),
      host_(std::move(host)),
      port_(port),
      owned_message_pool_(
          common::ObjectPool<common::LinearBuffer>::create<pool_chunk>(wire::frame::max_frame_size)
      ),
      message_pool_(message_pool),
      connection_(wire::frame::fit_rx_capacity(rx_buffer_size)),
      backoff_(backoff) {
    if (auto const fitted = wire::frame::fit_rx_capacity(rx_buffer_size);
        fitted != rx_buffer_size) {
        LOG_TRANSPORT_RX_BUFFER_ADJUST(rx_buffer_size, fitted);
    }
}

Connector::~Connector() {
    if (connection_.registered()) {
        reactor_.remove(connection_.channel());
    }

    timer_scheduler_.cancel(reconnect_timer_);
    for (auto const timer : app_timer_) {
        timer_scheduler_.cancel(timer);
    }
    // connection_ 소멸 시 소켓을 닫고 송신 버퍼를 풀에 반환한다.
}

void Connector::notify_registered() {
    // Agent 등록에 성공하면 다음 재연결은 기본 대기 시간부터 시작한다.
    backoff_.reset();
}

void Connector::disconnect(port::DisconnectReason reason) {
    disconnect_and_reconnect(reason);
}

port::MessageBuffer Connector::make_message_buffer() {
    auto buf = message_pool_.acquire();
    // 프레임 헤더 공간을 확보한다. 실패 로그는 send()에서 한 번만 남긴다.
    (void)buf->set_headroom(wire::frame::header_size);

    return buf;
}

void Connector::send(port::MessageBuffer message) {
    if (connection_.state() != Connection::State::connected) {
        return; // 연결되지 않았으면 메시지를 버린다.
    }

    // 메시지의 [type][body] 앞에 프레임 헤더를 붙인다.
    if (!wire::frame::encode_frame(*message)) {
        // 메시지 크기나 헤더 공간이 프레임 조건을 만족하지 않는다.
        LOG_TRANSPORT_FRAME_ENCODE_FAIL(message->data_span().size());

        return;
    }

    connection_.tx_enqueue(std::move(message));

    update_interests(); // 송신할 데이터가 있으면 쓰기 가능 이벤트를 받는다.
}

void Connector::schedule_timer(port::TimerSlot id, std::chrono::nanoseconds delay) {
    auto& slot = app_timer_.at(static_cast<std::size_t>(id));
    if (slot.valid()) {
        timer_scheduler_.cancel(slot); // 같은 슬롯의 기존 예약을 취소한다.
    }
    slot = timer_scheduler_.schedule(delay, *this);
}

void Connector::cancel_timer(port::TimerSlot id) {
    auto& slot = app_timer_.at(static_cast<std::size_t>(id));
    if (slot.valid()) {
        timer_scheduler_.cancel(slot);
        slot = io::TimerToken{};
    }
}

void Connector::on_expired(io::TimerToken id) {
    if (id == reconnect_timer_) {
        reconnect_timer_ = io::TimerToken{};
        connect();

        return;
    }

    for (std::size_t i = 0; i < port::timer_slot_count; ++i) {
        if (app_timer_.at(i) == id) {
            // on_timer()에서 다시 예약할 수 있도록 기존 토큰을 먼저 비운다.
            app_timer_.at(i) = io::TimerToken{};
            handler_->on_timer(static_cast<port::TimerSlot>(i));

            return;
        }
    }
    // 현재 예약과 일치하지 않는 만료 알림은 무시한다.
}

io::SysResult Connector::start() {
    if (handler_ == nullptr) {
        return io::SysResult::fail(); // init()이 먼저 호출되어야 한다.
    }
    // 연결 중이거나 연결된 상태에서는 중복 연결을 막는다.
    // 재연결 대기 중이면 connect()에서 예약을 취소하고 즉시 시도한다.
    if (connection_.state() != Connection::State::idle) {
        return io::SysResult::success();
    }

    connect();

    return io::SysResult::success();
}

void Connector::connect() {
    timer_scheduler_.cancel(reconnect_timer_);
    reconnect_timer_ = io::TimerToken{};

    int const raw = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (raw < 0) {
        LOG_TRANSPORT_CONNECT_FAIL(errno);
        schedule_reconnect();

        return;
    }

    io::Fd sock{raw};

    // ack와 outcome을 연속으로 보낼 때 TCP ACK 대기로 송신이 지연되지 않도록
    // Controller와 마찬가지로 Nagle 알고리즘을 끈다.
    int const nodelay = 1;
    (void)::setsockopt(sock.get(), IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port_);

    // 연결할 때마다 호스트 이름 또는 IPv4 주소를 동기적으로 해석한다.
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int const gai = ::getaddrinfo(host_.c_str(), nullptr, &hints, &res);
    if (gai != 0 || res == nullptr) {
        ++unresolved_attempts_;

        if (!host_unresolved_) {
            host_unresolved_ = true;
            LOG_TRANSPORT_HOST_RESOLVE_FAIL(host_, gai);
        }

        schedule_reconnect();

        return;
    }
    if (host_unresolved_) {
        host_unresolved_ = false;
        LOG_TRANSPORT_HOST_RESOLVE_RECOVER(host_, unresolved_attempts_);
        unresolved_attempts_ = 0;
    }

    addr.sin_addr = reinterpret_cast<sockaddr_in const*>(res->ai_addr)->sin_addr;
    ::freeaddrinfo(res);

    int r = 0;

    do {
        r = ::connect(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    } while (r < 0 && errno == EINTR);
    if (r < 0 && errno != EINPROGRESS) {
        LOG_TRANSPORT_CONNECT_FAIL(errno);
        schedule_reconnect();

        return;
    }

    connection_.init(*this, std::move(sock), connect_interest);
    connection_.transition(Connection::State::connecting);
    if (auto const result = reactor_.add(connection_.channel()); !result) {
        LOG_TRANSPORT_REACTOR_ADD_FAIL(result.err);
        connection_.close();
        schedule_reconnect();

        return;
    }

    LOG_TRANSPORT_CONNECT(host_, port_);
}

void Connector::on_connection_event(Connection& conn, io::ChannelEvents events) {
    switch (conn.state()) {
    case Connection::State::connecting:
        handle_connecting(events);
        break;
    case Connection::State::connected:
        handle_connected(events);
        break;
    case Connection::State::idle:
        break;
    }
}

void Connector::handle_connecting(io::ChannelEvents events) {
    if (io::contains(events, io::ChannelEvents::error) ||
        io::contains(events, io::ChannelEvents::hangup)) {
        disconnect_and_reconnect(port::DisconnectReason::connect_fail);

        return;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(connection_.fd(), SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        LOG_TRANSPORT_CONNECT_FAIL(err);
        disconnect_and_reconnect(port::DisconnectReason::connect_fail);

        return;
    }

    connection_.transition(Connection::State::connected);

    // TCP 연결 후에도 Agent 등록에 실패할 수 있으므로, 재시도 간격은
    // notify_registered()에서 등록 성공을 확인한 뒤 초기화한다.
    if (auto const result = reactor_.modify(connection_.channel(), read_interest); !result) {
        LOG_TRANSPORT_REACTOR_MODIFY_FAIL(result.err);
        disconnect_and_reconnect(port::DisconnectReason::io_error);

        return;
    }

    LOG_TRANSPORT_CONNECT_SUCCESS(host_, port_);
    handler_->on_connected(); // Agent 등록 절차를 시작한다.
}

void Connector::handle_connected(io::ChannelEvents events) {
    if (io::contains(events, io::ChannelEvents::error) ||
        io::contains(events, io::ChannelEvents::hangup)) {
        // 오류 또는 연결 종료 이벤트는 io_error로 처리한다.
        disconnect_and_reconnect(port::DisconnectReason::io_error);

        return;
    }

    if (io::contains(events, io::ChannelEvents::readable)) {
        for (;;) {
            auto const r = connection_.receive();

            wire::frame::dispatch_frames(
                message_pool_,
                // on_recv()에서 연결을 끊으면 nullptr를 반환해 프레임 처리를 멈춘다.
                [this]() -> common::CircularBuffer* {
                    return connection_.state() == Connection::State::connected
                               ? &connection_.rx_buffer()
                               : nullptr;
                },
                // [type][body]를 전달하고, 메시지 종류에 따른 처리는 Agent에 맡긴다.
                [this](port::MessageBuffer payload) { handler_->on_recv(std::move(payload)); },
                [this](wire::frame::DecodeResult reason) {
                    // 내부 버퍼 읽기 오류와 수신 프레임 형식 오류를 구분해 기록한다.
                    if (reason == wire::frame::DecodeResult::read_error) {
                        LOG_TRANSPORT_FRAME_DECODE_CORRUPT();
                    } else {
                        LOG_TRANSPORT_FRAME_DECODE_FAIL(wire::frame::to_string(reason));
                    }
                    disconnect_and_reconnect(port::DisconnectReason::frame_error);
                }
            );

            if (connection_.state() != Connection::State::connected) {
                return; // 프레임 처리 중 연결이 끊겼으면 수신을 중단한다.
            }
            if (r.code == net::ReceiveResult::Code::would_block) {
                break;
            }
            if (r.code == net::ReceiveResult::Code::full) {
                continue; // 프레임 처리로 확보한 공간에 이어서 수신한다.
            }
            if (r.code == net::ReceiveResult::Code::error) {
                LOG_TRANSPORT_RECEIVE_FAIL(r.err);
                disconnect_and_reconnect(port::DisconnectReason::io_error);

                return;
            }

            disconnect_and_reconnect(port::DisconnectReason::peer_closed);

            return;
        }
    }

    if (io::contains(events, io::ChannelEvents::writable)) {
        auto const r = connection_.transmit();
        if (r.code == net::TransmitResult::Code::error) {
            LOG_TRANSPORT_SEND_FAIL(r.err);
            disconnect_and_reconnect(port::DisconnectReason::io_error);

            return;
        }
    }

    update_interests();
}

void Connector::update_interests() {
    if (connection_.state() != Connection::State::connected) {
        return;
    }

    io::ChannelEvents desired = read_interest;
    if (!connection_.tx_empty()) {
        desired |= io::ChannelEvents::writable;
    }
    if (desired != connection_.io_interest()) {
        if (auto const result = reactor_.modify(connection_.channel(), desired); !result) {
            LOG_TRANSPORT_REACTOR_MODIFY_FAIL(result.err);
            disconnect_and_reconnect(port::DisconnectReason::io_error);

            return;
        }
    }
}

void Connector::disconnect_and_reconnect(port::DisconnectReason reason) {
    if (connection_.registered()) {
        reactor_.remove(connection_.channel());
    }

    connection_.close(); // 소켓과 버퍼를 정리하고 idle 상태로 전환한다.

    for (auto& slot : app_timer_) {
        if (slot.valid()) {
            timer_scheduler_.cancel(slot);
            slot = io::TimerToken{};
        }
    }

    if (handler_ != nullptr) {
        handler_->on_disconnected(); // Agent에 연결 종료를 알린다.
    }

    schedule_reconnect();

    LOG_TRANSPORT_DISCONNECT(port::to_string(reason));
}

void Connector::schedule_reconnect() {
    auto const delay = backoff_.next_delay();

    reconnect_timer_ = timer_scheduler_.schedule(delay, *this);

    LOG_TRANSPORT_RECONNECT_SCHEDULE(
        std::chrono::duration_cast<std::chrono::milliseconds>(delay).count()
    );
}

} // namespace ddcs::agent::infra::transport
