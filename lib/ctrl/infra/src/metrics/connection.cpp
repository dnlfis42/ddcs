#include "ddcs/ctrl/infra/metrics/connection.hpp"

#include "ddcs/ctrl/infra/metrics/server.hpp"

#include <utility>

#include <cerrno>
#include <cstddef>

#include <sys/socket.h>
#include <sys/types.h>

namespace ddcs::ctrl::infra::metrics {

namespace {
constexpr std::size_t request_cap{8192}; // 요청 상한 - 넘으면 그만 읽고 응답(악성/과대 방어)
} // namespace

void Connection::on_io(std::uint32_t events) { server_->on_event(*this, events); }

void Connection::assign(common::Fd fd) noexcept {
    fd_ = std::move(fd);
    state_ = State::reading;
    in_epoll_ = false;
    rx_.clear();
    tx_.clear();
    tx_pos_ = 0;
}

void Connection::reset() noexcept {
    fd_.reset();
    state_ = State::idle;
    in_epoll_ = false;
    rx_.clear();
    tx_.clear();
    tx_pos_ = 0;
}

Connection::IoResult Connection::receive() {
    char buf[1024];
    for (;;) {
        ssize_t n;
        do {
            n = ::recv(fd_.get(), buf, sizeof(buf), 0);
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
            n = ::send(fd_.get(), tx_.data() + tx_pos_, tx_.size() - tx_pos_, MSG_NOSIGNAL);
        } while (n < 0 && errno == EINTR);
        if (n > 0) {
            tx_pos_ += static_cast<std::size_t>(n);
            continue;
        }
        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return IoResult::would_block; // 커널 송신 버퍼 포화 -> EPOLLOUT 대기
        }
        return IoResult::error;
    }
    state_ = State::done;
    return IoResult::ok;
}

} // namespace ddcs::ctrl::infra::metrics
