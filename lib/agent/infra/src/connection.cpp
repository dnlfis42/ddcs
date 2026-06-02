#include "ddcs/agent/infra/connection.hpp"

#include "ddcs/agent/infra/connector.hpp"

#include <cassert>
#include <cerrno>
#include <utility>

#include <cstddef>

#include <sys/socket.h>
#include <sys/types.h>

namespace ddcs::agent::infra {

namespace {

bool can_transition(Connection::State from, Connection::State to) noexcept {
    using S = Connection::State;
    switch (from) {
    case S::idle:
        return to == S::connecting;
    case S::connecting:
        return to == S::connected;
    case S::connected:
        return false; // 끊김은 reset() 으로 직접 idle 화
    }
    return false;
}

} // namespace

// 정책 없음: Reactor 가 알려온 readiness 를 Connector 로 곧장 위임.
void Connection::on_io(std::uint32_t events) { connector_->on_connection_event(*this, events); }

void Connection::assign(common::Fd fd, std::uint32_t io_interest) noexcept {
    assert(state_ == State::idle && "assign() on non-idle connection");
    fd_ = std::move(fd);
    io_interest_ = io_interest;
}

bool Connection::transition(State to) noexcept {
    if (!can_transition(state_, to)) {
        return false;
    }
    state_ = to;
    return true;
}

void Connection::reset() noexcept {
    fd_.reset();
    io_interest_ = 0;
    state_ = State::idle;
    in_epoll_ = false;
    rx_buffer_.clear();
    while (!tx_queue_.empty()) {
        tx_queue_.pop();
    }
}

// ET: 더 읽을 게 없을 때(EAGAIN)까지 소진. 전이는 하지 않고 결과만 보고.
Connection::IoResult Connection::receive() {
    for (;;) {
        auto dst = rx_buffer_.writable();
        if (dst.empty()) {
            return IoResult::full;
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
        return IoResult::error;
    }
}

// ET: 커널 송신 버퍼가 막힐 때(EAGAIN)까지 큐를 비운다.
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
            return IoResult::would_block; // 커널 버퍼 포화 -> EPOLLOUT 무장
        }
        return IoResult::error;
    }
    return IoResult::ok;
}

} // namespace ddcs::agent::infra
