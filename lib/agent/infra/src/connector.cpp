#include "ddcs/agent/infra/connector.hpp"

#include "ddcs/logger/log.hpp"
#include "ddcs/proto/frame/frame.hpp"
#include "ddcs/runtime/reactor.hpp"
#include "ddcs/runtime/timer_scheduler.hpp"

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace ddcs::agent::infra {

namespace {

constexpr std::size_t pool_chunk{64};
constexpr std::size_t payload_buf_capacity{proto::frame::header_size + proto::frame::length_limit};
constexpr std::uint32_t connect_interest{EPOLLOUT | EPOLLET}; // 완료를 EPOLLOUT 으로 감지
constexpr std::uint32_t read_interest{EPOLLIN | EPOLLET};

} // namespace

Connector::Connector(runtime::Reactor& reactor, runtime::TimerScheduler& timers, std::string host, std::uint16_t port)
    : reactor_{reactor}, timers_{timers}, host_{std::move(host)}, port_{port},
      payload_pool_{common::make_object_pool<common::LinearBuffer>(0, pool_chunk, payload_buf_capacity)} {}

Connector::~Connector() {
    if (connection_.in_epoll()) {
        reactor_.del(connection_.fd());
    }
    // connection_ dtor 가 fd 를 닫고, 타이머는 TimerScheduler 와 함께 소멸.
}

void Connector::start() { try_connect(); }

// --- Outbound -------------------------------------------------------------

common::PoolHandle<common::LinearBuffer> Connector::payload_buffer() {
    auto buf = payload_pool_.acquire();
    buf->reserve_front(proto::frame::header_size); // frame header 자리 확보
    return buf;
}

void Connector::send(std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) {
    if (connection_.state() != Connection::State::connected) {
        return; // 미연결 -> 드롭
    }
    if (body->size() > proto::frame::length_limit) {
        assert(false && "payload length exceeds length_limit");
        return;
    }
    auto const hdr = proto::frame::encode(
        {.magic = proto::frame::magic, .type = type, .length = static_cast<std::uint16_t>(body->size())}
    );
    if (!body->write_front({hdr.data(), hdr.size()})) {
        assert(false && "payload_buffer() 로 받지 않은 버퍼 - headroom 없음");
        return;
    }
    connection_.tx_enqueue(std::move(body));
    update_interest(); // EPOLLOUT 무장
}

void Connector::schedule_timer(TimerId id, std::chrono::nanoseconds delay) {
    auto& slot = app_timer_.at(slot_of(id));
    if (slot.valid()) {
        timers_.cancel(slot); // reschedule = 기존 취소
    }
    slot = timers_.schedule(delay, this);
}

void Connector::cancel_timer(TimerId id) {
    auto& slot = app_timer_.at(slot_of(id));
    if (slot.valid()) {
        timers_.cancel(slot);
        slot = runtime::TimerId{};
    }
}

void Connector::close() { disconnect_and_reconnect(); }

// --- runtime::TimerHandler -----------------------------------------------------

void Connector::on_timer_event(runtime::TimerId id) {
    if (id == reconnect_timer_) {
        reconnect_timer_ = runtime::TimerId{};
        try_connect();
        return;
    }
    for (std::size_t i = 0; i < timer_slot_count; ++i) {
        if (app_timer_.at(i) == id) {
            app_timer_.at(i) = runtime::TimerId{}; // one-shot 소비 (app 이 on_timer 안에서 재예약 가능)
            handler_->on_timer(static_cast<TimerId>(i));
            return;
        }
    }
    // 취소 후 잔여 등 - 무시
}

// --- 연결 수명 ------------------------------------------------------------

void Connector::try_connect() {
    reconnect_timer_ = runtime::TimerId{};

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
    // 호스트네임(DNS) 또는 숫자 IP 해석. 단일 연결 client 이므로 (재)연결 시 blocking resolve 1회 허용.
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
    connection_.assign(std::move(sock), connect_interest);
    (void)connection_.transition(Connection::State::connecting);
    if (!reactor_.add(connection_.fd(), connection_.io_interest(), &connection_)) {
        LOG_ERROR("agent_transport.epoll_add_fail");
        connection_.reset();
        arm_reconnect();
        return;
    }
    connection_.enter_epoll();
    LOG_DEBUG("agent_transport.connecting", ddcs::logger::kv("host", host_), ddcs::logger::kv("port", port_));
}

void Connector::on_connection_event(Connection& conn, std::uint32_t events) {
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

void Connector::on_connecting(std::uint32_t events) {
    if ((events & (EPOLLERR | EPOLLHUP)) != 0u) {
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
    if (!reactor_.mod(connection_.fd(), read_interest)) {
        disconnect_and_reconnect();
        return;
    }
    connection_.set_io_interest(read_interest);
    LOG_INFO("agent_transport.connected", ddcs::logger::kv("host", host_), ddcs::logger::kv("port", port_));
    handler_->on_connected(); // app -> register 시작 (send 가 EPOLLOUT 무장)
}

void Connector::on_connected_io(std::uint32_t events) {
    if ((events & (EPOLLERR | EPOLLHUP)) != 0u) {
        disconnect_and_reconnect();
        return;
    }
    if ((events & EPOLLIN) != 0u) {
        for (;;) {
            auto const r = connection_.receive();
            framing();
            if (connection_.state() != Connection::State::connected) {
                return; // framing 이 close->reconnect 유발
            }
            if (r == Connection::IoResult::would_block || r == Connection::IoResult::ok) {
                break;
            }
            if (r == Connection::IoResult::full) {
                continue; // framing 이 공간 확보 -> 더 읽기
            }
            disconnect_and_reconnect(); // peer_closed / error
            return;
        }
    }
    if ((events & EPOLLOUT) != 0u) {
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
        if (connection_.rx_size() < proto::frame::header_size) {
            return;
        }
        proto::frame::HeaderBytes hb{};
        connection_.rx_peek({hb.data(), hb.size()});
        auto const parsed_header = proto::frame::parse(hb);
        if (!parsed_header) {
            LOG_WARN("agent_transport.bad_magic");
            disconnect_and_reconnect();
            return;
        }
        auto const header = *parsed_header;
        std::size_t const total = proto::frame::header_size + header.length;
        if (total > inbound_buffer_capacity) {
            LOG_WARN("agent_transport.frame_too_long");
            disconnect_and_reconnect();
            return;
        }
        if (connection_.rx_size() < total) {
            return; // 부분 프레임
        }

        connection_.rx_consume(proto::frame::header_size);
        auto payload = payload_pool_.acquire();
        if (header.length > 0) {
            auto const w = payload->writable();
            connection_.rx_read({w.data(), header.length});
            payload->commit(header.length);
        }
        handler_->on_recv(header.type, std::move(payload));
        if (connection_.state() != Connection::State::connected) {
            return; // on_recv 중 app 이 close()
        }
    }
}

void Connector::update_interest() {
    if (connection_.state() != Connection::State::connected) {
        return;
    }
    std::uint32_t desired = read_interest;
    if (!connection_.tx_empty()) {
        desired |= EPOLLOUT;
    }
    if (desired != connection_.io_interest()) {
        if (!reactor_.mod(connection_.fd(), desired)) {
            disconnect_and_reconnect();
            return;
        }
        connection_.set_io_interest(desired);
    }
}

void Connector::disconnect_and_reconnect() {
    if (connection_.in_epoll()) {
        reactor_.del(connection_.fd());
    }
    connection_.reset(); // fd close(FIN) + 버퍼 비움 -> idle
    cancel_app_timers();
    if (handler_ != nullptr) {
        handler_->on_disconnected(); // app FSM -> idle (멱등)
    }
    arm_reconnect();
    LOG_DEBUG("agent_transport.disconnected");
}

void Connector::arm_reconnect() {
    auto const delay = backoff_.next_delay();
    reconnect_timer_ = timers_.schedule(delay, this);
    LOG_DEBUG(
        "agent_transport.reconnect_scheduled",
        ddcs::logger::kv("delay_ms", std::chrono::duration_cast<std::chrono::milliseconds>(delay).count())
    );
}

void Connector::cancel_app_timers() {
    for (auto& slot : app_timer_) {
        if (slot.valid()) {
            timers_.cancel(slot);
            slot = runtime::TimerId{};
        }
    }
}

} // namespace ddcs::agent::infra
