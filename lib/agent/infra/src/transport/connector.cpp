#include "ddcs/agent/infra/transport/connector.hpp"

#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_scheduler.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/frame/extract.hpp"
#include "ddcs/wire/frame/frame.hpp"
#include "ddcs/wire/frame/seal.hpp"

#include <cassert>
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
// 송신/프레이밍 payload 상한은 controller와 공유하는 wire 규칙(max_payload_length)을 따른다.
constexpr std::size_t payload_buf_capacity =
    wire::frame::header_size + wire::frame::max_payload_length;
// 최대 frame(header + max payload)이 rx ring에 통째로 들어가야 부분 frame 대기가 끝난다(아니면
// framing 교착).
static_assert(
    payload_buf_capacity <= rx_buffer_capacity,
    "max frame (header + max_payload_length) must fit in the rx ring buffer"
);
// 완료를 writable로 감지
constexpr io::ChannelEvents connect_interest{
    io::ChannelEvents::writable | io::ChannelEvents::edge_triggered
};
constexpr io::ChannelEvents read_interest{
    io::ChannelEvents::readable | io::ChannelEvents::edge_triggered
};

} // namespace

Connector::Connector(
    io::Reactor& reactor, io::TimerScheduler& timers, std::string host, std::uint16_t port,
    std::chrono::nanoseconds reconnect_base, std::chrono::nanoseconds reconnect_max
)
    : reactor_(reactor),
      timers_(timers),
      host_(std::move(host)),
      port_(port),
      payload_pool_(
          common::ObjectPool<common::LinearBuffer>::create<pool_chunk>(payload_buf_capacity)
      ),
      backoff_(reconnect_base, reconnect_max) {}

Connector::~Connector() {
    if (connection_.registered()) {
        reactor_.remove(connection_.channel());
    }
    // connection_ dtor가 fd를 닫고, 타이머는 TimerScheduler와 함께 소멸
}

void Connector::start() {
    try_connect();
}

common::PoolHandle<common::LinearBuffer> Connector::payload_buffer() {
    auto buf = payload_pool_.acquire();
    bool const reserved = wire::frame::reserve_header_room(*buf); // frame header 자리 확보
    assert(reserved);
    (void)reserved;
    return buf;
}

void Connector::send(common::PoolHandle<common::LinearBuffer> message) {
    if (connection_.state() != Connection::State::connected) {
        return; // 미연결이면 드롭
    }
    // message는 메시지 통째(`[type][body]`). frame은 length만 싣는다.
    if (!wire::frame::seal(*message)) {
        assert(false && "payload 상한 초과 또는 payload_buffer()를 거치지 않은 버퍼");
        return;
    }
    connection_.tx_enqueue(std::move(message));
    update_interest(); // writable 무장
}

void Connector::schedule_timer(port::TimerSlot id, std::chrono::nanoseconds delay) {
    auto& slot = app_timer_.at(slot_of(id));
    if (slot.valid()) {
        timers_.cancel(slot); // reschedule = 기존 취소
    }
    slot = timers_.schedule(delay, *this);
}

void Connector::cancel_timer(port::TimerSlot id) {
    auto& slot = app_timer_.at(slot_of(id));
    if (slot.valid()) {
        timers_.cancel(slot);
        slot = io::TimerId{};
    }
}

void Connector::close() {
    disconnect_and_reconnect();
}

void Connector::notify_registered() {
    // app 등록 성공: 다음 끊김부터 backoff를 base에서 다시 시작
    backoff_.reset();
}

void Connector::on_expired(io::TimerId id) {
    if (id == reconnect_timer_) {
        reconnect_timer_ = io::TimerId{};
        try_connect();
        return;
    }
    for (std::size_t i = 0; i < timer_slot_count; ++i) {
        if (app_timer_.at(i) == id) {
            app_timer_.at(i) = io::TimerId{}; // one-shot 소비 (app이 on_timer 안에서 재예약 가능)
            handler_->on_timer(static_cast<port::TimerSlot>(i));
            return;
        }
    }
    // 취소 후 잔여 등은 무시
}

void Connector::try_connect() {
    reconnect_timer_ = io::TimerId{};

    int const raw = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (raw < 0) {
        LOG_WARN("agent_transport.socket_fail", logger::kv("errno", errno));
        arm_reconnect();
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
    if (::getaddrinfo(host_.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
        LOG_ERROR("agent_transport.bad_host", logger::kv("host", host_));
        arm_reconnect();
        return;
    }
    addr.sin_addr = reinterpret_cast<sockaddr_in const*>(res->ai_addr)->sin_addr;
    ::freeaddrinfo(res);

    int r = 0;
    do {
        r = ::connect(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    } while (r < 0 && errno == EINTR);
    if (r < 0 && errno != EINPROGRESS) {
        LOG_WARN("agent_transport.connect_fail", logger::kv("errno", errno));
        arm_reconnect();
        return;
    }

    connection_.set_connector(*this);
    if (!connection_.assign(std::move(sock), connect_interest)) {
        LOG_ERROR("agent_transport.channel_init_fail");
        connection_.reset();
        arm_reconnect();
        return;
    }
    (void)connection_.transition(Connection::State::connecting);
    if (!reactor_.add(connection_.channel())) {
        LOG_ERROR("agent_transport.epoll_add_fail");
        connection_.reset();
        arm_reconnect();
        return;
    }

    LOG_DEBUG("agent_transport.connecting", logger::kv("host", host_), logger::kv("port", port_));
}

void Connector::on_connection_event(Connection& conn, io::ChannelEvents events) {
    switch (conn.state()) {
    case Connection::State::connecting:
        on_connecting(events);
        break;
    case Connection::State::connected:
        on_connected_io(events);
        break;
    case Connection::State::idle:
        break;
    }
}

void Connector::on_connecting(io::ChannelEvents events) {
    if (io::contains(events, io::ChannelEvents::error) ||
        io::contains(events, io::ChannelEvents::hangup)) {
        disconnect_and_reconnect();
        return;
    }

    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(connection_.fd(), SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        LOG_WARN("agent_transport.connect_error", logger::kv("errno", err));
        disconnect_and_reconnect();
        return;
    }

    (void)connection_.transition(Connection::State::connected);
    // backoff_.reset()은 여기(TCP 연결)가 아니라 app 등록 성공 시(notify_registered)에 한다.
    // TCP는 붙지만 등록이 안 끝나는 controller를 상대로 backoff가 지수적으로 자라게 하기 위함.
    if (!reactor_.modify(connection_.channel(), read_interest)) {
        disconnect_and_reconnect();
        return;
    }

    LOG_INFO("agent_transport.connected", logger::kv("host", host_), logger::kv("port", port_));
    handler_->on_connected(); // app이 register 시작 (send가 writable 무장)
}

void Connector::on_connected_io(io::ChannelEvents events) {
    if (io::contains(events, io::ChannelEvents::error) ||
        io::contains(events, io::ChannelEvents::hangup)) {
        disconnect_and_reconnect();
        return;
    }

    if (io::contains(events, io::ChannelEvents::readable)) {
        for (;;) {
            auto const r = connection_.receive();
            framing();
            if (connection_.state() != Connection::State::connected) {
                return; // framing이 연결 종료와 재연결을 유발
            }
            if (r == Connection::IoResult::would_block || r == Connection::IoResult::ok) {
                break;
            }
            if (r == Connection::IoResult::full) {
                continue; // framing이 공간 확보 후 더 읽기
            }
            disconnect_and_reconnect(); // peer_closed / error
            return;
        }
    }

    if (io::contains(events, io::ChannelEvents::writable)) {
        if (connection_.transmit() == Connection::IoResult::error) {
            disconnect_and_reconnect();
            return;
        }
    }

    update_interest();
}

// rx 버퍼에서 완성 프레임을 모두 추출해 위로 올린다(ctrl Server::dispatch_frames와 공유 루프).
void Connector::framing() {
    wire::frame::extract_frames(
        payload_pool_,
        // get_rx: 연결이 살아있는 동안만 rx ring 제공(on_recv 중 close되면 nullptr -> 종료)
        [this]() -> common::CircularBuffer* {
            return connection_.state() == Connection::State::connected ? &connection_.rx_buffer()
                                                                       : nullptr;
        },
        // payload = msg `[type][body]` 통째. type 디스패치는 app(message_type)이 한다.
        [this](common::PoolHandle<common::LinearBuffer> payload) {
            handler_->on_recv(std::move(payload));
        },
        [this](wire::frame::PullResult reason) {
            if (reason == wire::frame::PullResult::bad_magic) {
                LOG_WARN("agent_transport.bad_magic");
            } else if (reason == wire::frame::PullResult::too_long) {
                LOG_WARN("agent_transport.frame_too_long");
            }
            disconnect_and_reconnect();
        }
    );
}

void Connector::update_interest() {
    if (connection_.state() != Connection::State::connected) {
        return;
    }
    io::ChannelEvents desired = read_interest;
    if (!connection_.tx_empty()) {
        desired |= io::ChannelEvents::writable;
    }
    if (desired != connection_.io_interest()) {
        if (!reactor_.modify(connection_.channel(), desired)) {
            disconnect_and_reconnect();
            return;
        }
    }
}

void Connector::disconnect_and_reconnect() {
    if (connection_.registered()) {
        reactor_.remove(connection_.channel());
    }
    connection_.reset(); // fd close(FIN) + 버퍼 비움으로 idle화
    cancel_app_timers();
    if (handler_ != nullptr) {
        handler_->on_disconnected(); // app FSM을 idle로 (멱등)
    }
    arm_reconnect();
    LOG_DEBUG("agent_transport.disconnected");
}

void Connector::arm_reconnect() {
    auto const delay = backoff_.next_delay();
    reconnect_timer_ = timers_.schedule(delay, *this);

    LOG_DEBUG(
        "agent_transport.reconnect_scheduled",
        logger::kv("delay_ms", std::chrono::duration_cast<std::chrono::milliseconds>(delay).count())
    );
}

void Connector::cancel_app_timers() {
    for (auto& slot : app_timer_) {
        if (slot.valid()) {
            timers_.cancel(slot);
            slot = io::TimerId{};
        }
    }
}

} // namespace ddcs::agent::infra::transport
