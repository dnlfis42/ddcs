#include "ddcs/ctrl/infra/prometheus/connection.hpp"

#include "ddcs/ctrl/infra/prometheus/server.hpp"

#include <cerrno>
#include <cstddef>
#include <utility>

#include <sys/socket.h>
#include <sys/types.h>

namespace ddcs::ctrl::infra::prometheus {

namespace {
constexpr std::size_t request_cap{8192}; // 요청 상한 - 넘으면 그만 읽고 응답(악성/과대 방어)
} // namespace

bool Connection::assign(Server& server, common::Fd fd, io::ChannelEvents interests) noexcept {
    if (state_ != State::idle) {
        return false;
    }
    if (!channel_.init(std::move(fd), interests, *this)) {
        return false;
    }
    server_ = &server;
    state_ = State::reading;
    rx_.clear();
    tx_.clear();
    tx_pos_ = 0;
    return true;
}

void Connection::reset() noexcept {
    server_ = nullptr;
    channel_.reset();
    state_ = State::idle;
    rx_.clear();
    tx_.clear();
    tx_pos_ = 0;
}

void Connection::on_ready(io::Channel& channel, io::ChannelEvents events) {
    if (&channel != &channel_) {
        return;
    }
    server_->handle_connection_ready(*this, events);
}

Connection::IoResult Connection::receive() {
    char buf[1024];
    for (;;) {
        ssize_t n;
        do {
            n = ::recv(channel_.fd(), buf, sizeof(buf), 0);
        } while (n < 0 && errno == EINTR);
        if (n > 0) {
            if (rx_.size() < request_cap) {
                rx_.append(buf, static_cast<std::size_t>(n));
            }
            continue; // ET: 소진까지
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

bool Connection::request_complete() const noexcept {
    return rx_.find("\r\n\r\n") != std::string::npos || rx_.size() >= request_cap;
}

void Connection::begin_response(std::string http) {
    tx_ = std::move(http);
    tx_pos_ = 0;
    state_ = State::writing;
}

Connection::IoResult Connection::transmit() {
    while (tx_pos_ < tx_.size()) {
        ssize_t n;
        do {
            n = ::send(channel_.fd(), tx_.data() + tx_pos_, tx_.size() - tx_pos_, MSG_NOSIGNAL);
        } while (n < 0 && errno == EINTR);
        if (n > 0) {
            tx_pos_ += static_cast<std::size_t>(n);
            continue;
        }
        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return IoResult::would_block; // 커널 송신 버퍼 포화 -> writable 대기
        }
        return IoResult::error;
    }
    state_ = State::done;
    return IoResult::ok;
}

} // namespace ddcs::ctrl::infra::prometheus
