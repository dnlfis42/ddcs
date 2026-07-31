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
#include <sys/socket.h>

namespace ddcs::agent::infra::transport {

namespace {

constexpr std::size_t pool_chunk = 64;

// 완료를 writable로 감지
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
      message_pool_(
          common::ObjectPool<common::LinearBuffer>::create<pool_chunk>(wire::frame::max_frame_size)
      ),
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
    // connection_ dtor가 fd를 닫고, 타이머는 TimerScheduler와 함께 소멸
}

void Connector::notify_registered() {
    // app 등록 성공: 다음 끊김부터 backoff를 base에서 다시 시작
    backoff_.reset();
}

void Connector::disconnect(port::DisconnectReason reason) {
    disconnect_and_reconnect(reason);
}

port::MessageBuffer Connector::make_message_buffer() {
    auto buf = message_pool_.acquire();
    // frame header 자리 확보. 실패해도 여기서는 알리지 않는다. 그 버퍼는 프레이밍이 안 되므로
    // send의 encode_frame이 같은 사실을 한 번 알린다.
    (void)buf->set_headroom(wire::frame::header_size);
    return buf;
}

void Connector::send(port::MessageBuffer message) {
    if (connection_.state() != Connection::State::connected) {
        return; // 미연결이면 드롭
    }
    // message는 메시지 통째(`[type][body]`). frame은 length만 싣는다.
    if (!wire::frame::encode_frame(*message)) {
        // payload 상한 초과 또는 make_message_buffer()를 거치지 않은 버퍼(프로그래머 오류)
        LOG_TRANSPORT_FRAME_ENCODE_FAIL(message->data_span().size());
        return;
    }
    connection_.tx_enqueue(std::move(message));
    update_interests(); // writable 관심을 켠다
}

void Connector::schedule_timer(port::TimerSlot id, std::chrono::nanoseconds delay) {
    auto& slot = app_timer_.at(static_cast<std::size_t>(id));
    if (slot.valid()) {
        timer_scheduler_.cancel(slot); // reschedule = 기존 취소
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
            app_timer_.at(i) =
                io::TimerToken{}; // one-shot 소비 (app이 on_timer 안에서 재예약 가능)
            handler_->on_timer(static_cast<port::TimerSlot>(i));
            return;
        }
    }
    // 취소 후 잔여 등은 무시
}

io::SysResult Connector::start() {
    if (handler_ == nullptr) {
        return io::SysResult::fail(); // init 전 start
    }
    // 이미 연결 중이거나 연결된 상태면 아무것도 하지 않는다. 그대로 connect()로 가면
    // Connection::init의 전제조건을 어겨 죽는다.
    // backoff 재연결을 기다리는 중(connection_은 idle)은 막지 않는다. 그때의 start()는
    // "지금 바로 재시도"로 동작하고, connect()가 첫 줄에서 예약 토큰을 비워 준다.
    if (connection_.state() != Connection::State::idle) {
        return io::SysResult::success();
    }

    connect();
    return io::SysResult::success();
}

void Connector::connect() {
    reconnect_timer_ = io::TimerToken{};

    int const raw = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (raw < 0) {
        LOG_TRANSPORT_CONNECT_FAIL(errno);
        schedule_reconnect();
        return;
    }
    io::Fd sock{raw};

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port_);
    // 호스트네임(DNS) 또는 숫자 IP 해석
    // 단일 연결 client 이므로 (재)연결 시 blocking resolve 1회 허용
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
    // backoff_.reset()은 여기(TCP 연결)가 아니라 app 등록 성공 시(notify_registered)에 한다.
    // TCP는 붙지만 등록이 안 끝나는 controller를 상대로 backoff가 지수적으로 자라게 하기 위함.
    if (auto const result = reactor_.modify(connection_.channel(), read_interest); !result) {
        LOG_TRANSPORT_REACTOR_MODIFY_FAIL(result.err);
        disconnect_and_reconnect(port::DisconnectReason::io_error);
        return;
    }

    LOG_TRANSPORT_CONNECT_SUCCESS(host_, port_);
    handler_->on_connected(); // app이 register 시작 (send가 writable 관심을 켠다)
}

void Connector::handle_connected(io::ChannelEvents events) {
    if (io::contains(events, io::ChannelEvents::error) ||
        io::contains(events, io::ChannelEvents::hangup)) {
        // epoll이 둘을 한 비트로 뭉개므로 여기서는 peer_closed와 갈라내지 못한다.
        disconnect_and_reconnect(port::DisconnectReason::io_error);
        return;
    }

    if (io::contains(events, io::ChannelEvents::readable)) {
        for (;;) {
            auto const r = connection_.receive();

            wire::frame::dispatch_frames(
                message_pool_,
                // get_rx: 연결이 살아있는 동안만 rx ring 제공(on_recv 중 close되면 nullptr -> 종료)
                [this]() -> common::CircularBuffer* {
                    return connection_.state() == Connection::State::connected
                               ? &connection_.rx_buffer()
                               : nullptr;
                },
                // payload = msg `[type][body]` 통째. type 디스패치는 app(message_type)이 한다.
                [this](port::MessageBuffer payload) { handler_->on_recv(std::move(payload)); },
                [this](wire::frame::DecodeResult reason) {
                    // bad_magic/too_long은 상대가 잘못 보낸 것이고, read_error는 우리 ring
                    // 로직이 어긋난 것이라 레벨이 갈린다.
                    if (reason == wire::frame::DecodeResult::read_error) {
                        LOG_TRANSPORT_FRAME_DECODE_CORRUPT();
                    } else {
                        LOG_TRANSPORT_FRAME_DECODE_FAIL(wire::frame::to_string(reason));
                    }
                    disconnect_and_reconnect(port::DisconnectReason::frame_error);
                }
            );

            if (connection_.state() != Connection::State::connected) {
                return; // framing이 연결 종료와 재연결을 유발
            }
            if (r.code == net::ReceiveResult::Code::would_block) {
                break;
            }
            if (r.code == net::ReceiveResult::Code::full) {
                continue; // framing이 공간 확보 후 더 읽기
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
    connection_.close(); // fd close(FIN) + 버퍼 비움으로 idle화

    // cancel timer
    for (auto& slot : app_timer_) {
        if (slot.valid()) {
            timer_scheduler_.cancel(slot);
            slot = io::TimerToken{};
        }
    }

    if (handler_ != nullptr) {
        handler_->on_disconnected(); // app FSM을 idle로 (멱등)
    }
    schedule_reconnect();
    // reason이 왜 끊겼는지의 유일한 출처다.
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
