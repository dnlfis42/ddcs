#include "ddcs/agent/infra/frame/connector.hpp"

#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_scheduler.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/frame/frame.hpp"

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

namespace ddcs::agent::infra::frame {

namespace wire_frame = ddcs::wire::frame;

namespace {

constexpr std::size_t pool_chunk{64};
// frame이 실을 수 있는 payload 상한
//  최대 frame이 rx ring에 통째로 들어가야 부분 frame 대기가 끝난다.
constexpr std::size_t payload_capacity{inbound_buffer_capacity - wire_frame::header_size};
constexpr std::size_t payload_buf_capacity{wire_frame::header_size + payload_capacity};
// 완료를 writable로 감지
constexpr io::ChannelEvents connect_interest{
    io::ChannelEvents::writable | io::ChannelEvents::edge_triggered
};
constexpr io::ChannelEvents read_interest{
    io::ChannelEvents::readable | io::ChannelEvents::edge_triggered
};

} // namespace

Connector::Connector(
    io::Reactor& reactor, io::TimerScheduler& timers, std::string host, std::uint16_t port
)
    : reactor_{reactor},
      timers_{timers},
      host_{std::move(host)},
      port_{port},
      payload_pool_{
          common::ObjectPool<common::LinearBuffer>::create<pool_chunk>(payload_buf_capacity)
      } {}

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
    bool const reserved = buf->try_grow_headroom(wire_frame::header_size); // frame header 자리 확보
    assert(reserved);
    (void)reserved;
    return buf;
}

void Connector::send(common::PoolHandle<common::LinearBuffer> message) {
    if (connection_.state() != Connection::State::connected) {
        return; // 미연결이면 드롭
    }
    // message는 acmp 메시지 통째(`[type][body]`). frame은 length만 싣는다.
    if (message->size() > payload_capacity) {
        assert(false && "payload length exceeds payload_capacity");
        return;
    }
    auto const hdr = wire_frame::encode(static_cast<std::uint16_t>(message->size()));
    if (!message->try_prepend({hdr.data(), hdr.size()})) {
        assert(false && "payload_buffer() 로 받지 않은 버퍼 - headroom 없음");
        return;
    }
    connection_.tx_enqueue(std::move(message));
    update_interest(); // writable 무장
}

void Connector::schedule_timer(TimerId id, std::chrono::nanoseconds delay) {
    auto& slot = app_timer_.at(slot_of(id));
    if (slot.valid()) {
        timers_.cancel(slot); // reschedule = 기존 취소
    }
    slot = timers_.schedule(delay, *this);
}

void Connector::cancel_timer(TimerId id) {
    auto& slot = app_timer_.at(slot_of(id));
    if (slot.valid()) {
        timers_.cancel(slot);
        slot = io::TimerId{};
    }
}

void Connector::close() {
    disconnect_and_reconnect();
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
            handler_->on_timer(static_cast<TimerId>(i));
            return;
        }
    }
    // 취소 후 잔여 등은 무시
}

void Connector::try_connect() {
    reconnect_timer_ = io::TimerId{};

    int const raw = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (raw < 0) {
        LOG_WARN("agent_transport.socket_fail", ddcs::logger::kv("errno", errno));
        arm_reconnect();
        return;
    }
    common::Fd sock{raw};

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
        LOG_ERROR("agent_transport.bad_host", ddcs::logger::kv("host", host_));
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
        LOG_WARN("agent_transport.connect_fail", ddcs::logger::kv("errno", errno));
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

    LOG_DEBUG(
        "agent_transport.connecting", ddcs::logger::kv("host", host_),
        ddcs::logger::kv("port", port_)
    );
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
        LOG_WARN("agent_transport.connect_error", ddcs::logger::kv("errno", err));
        disconnect_and_reconnect();
        return;
    }

    (void)connection_.transition(Connection::State::connected);
    backoff_.reset();
    if (!reactor_.modify(connection_.channel(), read_interest)) {
        disconnect_and_reconnect();
        return;
    }

    LOG_INFO(
        "agent_transport.connected", ddcs::logger::kv("host", host_),
        ddcs::logger::kv("port", port_)
    );
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

// rx 버퍼에서 완성 프레임을 모두 추출해 위로 올린다.
void Connector::framing() {
    for (;;) {
        if (connection_.rx_size() < wire_frame::header_size) {
            return;
        }

        wire_frame::HeaderBytes hb{};
        connection_.rx_peek({hb.data(), hb.size()});
        auto const parsed_length = wire_frame::decode(hb);
        if (!parsed_length) {
            LOG_WARN("agent_transport.bad_magic");
            disconnect_and_reconnect();
            return;
        }

        std::uint16_t const payload_length = *parsed_length;
        if (payload_length > payload_capacity) {
            LOG_WARN("agent_transport.frame_too_long");
            disconnect_and_reconnect();
            return;
        }

        std::size_t const total = wire_frame::header_size + payload_length;
        if (connection_.rx_size() < total) {
            return; // 부분 프레임
        }

        if (!connection_.rx_consume(wire_frame::header_size)) {
            disconnect_and_reconnect();
            return;
        }
        auto payload = payload_pool_.acquire();
        if (payload_length > 0) {
            auto const w = payload->tailroom_span();
            if (!connection_.rx_read({w.data(), payload_length}) ||
                !payload->try_commit(payload_length)) {
                disconnect_and_reconnect();
                return;
            }
        }
        // payload = acmp `[type][body]` 통째. type 디스패치는 app(peek_type)이 한다.
        handler_->on_recv(std::move(payload));
        if (connection_.state() != Connection::State::connected) {
            return; // on_recv 중 app이 close()
        }
    }
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
        ddcs::logger::kv(
            "delay_ms", std::chrono::duration_cast<std::chrono::milliseconds>(delay).count()
        )
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

} // namespace ddcs::agent::infra::frame
