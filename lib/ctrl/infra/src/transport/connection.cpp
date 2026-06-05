#include "ddcs/ctrl/infra/transport/connection.hpp"

#include "ddcs/ctrl/infra/transport/connection_coordinator.hpp"

#include <cassert>
#include <cerrno>
#include <utility>

#include <cstddef>

#include <sys/socket.h>
#include <sys/types.h>

namespace ddcs::ctrl::infra::transport {

namespace {

// closing -> idle 은 reset()(풀 반납) 경로.
bool can_transition(Connection::State from, Connection::State to) noexcept {
    using S = Connection::State;
    switch (from) {
    case S::idle:
        return to == S::open || to == S::closing;
    case S::open:
        return to == S::active_close || to == S::passive_close || to == S::aborting || to == S::closing;
    case S::active_close:
        return to == S::passive_wait || to == S::closing;
    case S::passive_wait:
        return to == S::closing;
    case S::passive_close:
        return to == S::aborting || to == S::closing;
    case S::aborting:
        return to == S::closing;
    case S::closing:
        return to == S::idle;
    }
    return false;
}

} // namespace

// 정책 없음: Reactor 가 알려온 readiness 를 coordinator 로 곧장 위임.
void Connection::on_fd_event(std::uint32_t events) { coordinator_->on_event(*this, events); }

void Connection::assign(ConnectionId id, common::Fd fd, Endpoint peer, std::uint32_t io_interest) noexcept {
    assert(state_ == State::idle && "assign() on non-idle connection");
    id_ = id;
    fd_ = std::move(fd);
    peer_ = peer;
    io_interest_ = io_interest;
}

bool Connection::transition(State to) noexcept {
    if (!can_transition(state_, to)) {
        return false;
    }
    state_ = to;
    return true;
}

void Connection::latch_rst() noexcept {
    if (!fd_) {
        return;
    }
    linger const lin{.l_onoff = 1, .l_linger = 0};
    ::setsockopt(fd_.get(), SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
}

void Connection::shutdown_write() noexcept {
    if (!fd_) {
        return;
    }
    ::shutdown(fd_.get(), SHUT_WR);
}

void Connection::reset() noexcept {
    id_.reset();
    fd_.reset();
    peer_.reset();
    io_interest_ = 0;
    state_ = State::idle;
    in_epoll_ = false;
    close_requested_ = false;
    rx_buffer_.clear();
    while (!tx_queue_.empty()) {
        tx_queue_.pop();
    }
    // coordinator_ 는 per-conn 상태가 아니라 슬롯 바인딩이므로 유지(재사용 시 set_coordinator 가 갱신).
}

// ET: 더 읽을 게 없을 때(EAGAIN)까지 소진. 전이는 하지 않고 결과만 보고한다.
Connection::IoResult Connection::receive() {
    for (;;) {
        auto dst = rx_buffer_.writable();
        if (dst.empty()) {
            return IoResult::full; // 버퍼 포화. 재개, 백프레셔는 ConnectionManager 정책
        }

        ssize_t n;
        do {
            n = ::recv(fd_.get(), dst.data(), dst.size(), 0);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            rx_buffer_.commit(static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            return IoResult::peer_closed; // FIN
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return IoResult::would_block;
        }
        return IoResult::error; // RST 등 복구 불가
    }
}

// ET: 커널 송신 버퍼가 막힐 때(EAGAIN)까지 큐를 비운다. 전이는 하지 않는다.
Connection::IoResult Connection::transmit() {
    while (!tx_queue_.empty()) {
        auto& buffer = *tx_queue_.front();
        auto data = buffer.readable();
        if (data.empty()) {
            tx_queue_.pop();
            continue;
        }

        ssize_t n;
        do {
            n = ::send(fd_.get(), data.data(), data.size(), MSG_NOSIGNAL);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            buffer.consume(static_cast<std::size_t>(n));
            if (buffer.size() == 0) {
                tx_queue_.pop();
            }
            continue;
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return IoResult::would_block; // 커널 버퍼 포화. ConnectionManager가 EPOLLOUT 무장
        }
        return IoResult::error; // EPIPE/ECONNRESET 등
    }
    return IoResult::ok; // 큐 전부 비움
}

} // namespace ddcs::ctrl::infra::transport
