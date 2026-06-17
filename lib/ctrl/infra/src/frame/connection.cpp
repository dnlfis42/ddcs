#include "ddcs/ctrl/infra/frame/connection.hpp"

#include "ddcs/ctrl/infra/frame/server.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <utility>

#include <sys/socket.h>
#include <sys/types.h>

namespace ddcs::ctrl::infra::frame {

bool Connection::init(
    Server& server, port::ConnectionId id, PeerAddress peer, common::Fd&& fd,
    io::ChannelEvents interests
) noexcept {
    if (state_ != State::idle) {
        return false;
    }
    if (!channel_.init(std::move(fd), interests, *this)) {
        return false;
    }

    server_ = &server;
    id_ = id;
    peer_ = peer;
    state_ = State::ready;
    return true;
}

void Connection::close() noexcept {
    assert(!channel_.registered());

    server_ = nullptr;
    id_.clear();
    peer_.clear();
    state_ = State::idle;
    channel_.close();
    rx_buffer_.clear();
    while (!tx_queue_.empty()) {
        tx_queue_.pop();
    }
}

void Connection::on_ready(io::Channel& channel, io::ChannelEvents events) {
    if (&channel != &channel_) {
        return;
    }
    server_->handle_connection_ready(*this, events);
}

bool Connection::mark_tracked() noexcept {
    if (state_ != State::ready && state_ != State::active) {
        return false;
    }
    state_ = State::tracked;
    return true;
}

bool Connection::mark_active() noexcept {
    if (state_ != State::tracked) {
        return false;
    }
    state_ = State::active;
    return true;
}

Connection::IoResult Connection::receive() {
    assert(state_ == State::active);

    for (;;) {
        auto dst = rx_buffer_.writable_span();
        if (dst.empty()) {
            return IoResult::full;
        }

        ssize_t n;
        do {
            n = ::recv(channel_.fd(), dst.data(), dst.size(), 0);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            if (!rx_buffer_.try_commit(static_cast<std::size_t>(n))) {
                return IoResult::error;
            }
            continue;
        }
        if (n == 0) {
            return IoResult::peer_closed;
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return IoResult::would_block;
        }
        return IoResult::error;
    }
}

Connection::IoResult Connection::transmit() {
    assert(state_ == State::active);

    while (!tx_queue_.empty()) {
        auto& buffer = *tx_queue_.front();
        auto data = buffer.data_span();
        if (data.empty()) {
            tx_queue_.pop();
            continue;
        }

        ssize_t n;
        do {
            n = ::send(channel_.fd(), data.data(), data.size(), MSG_NOSIGNAL);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            if (!buffer.try_consume(static_cast<std::size_t>(n))) {
                return IoResult::error;
            }
            if (buffer.size() == 0) {
                tx_queue_.pop();
            }
            continue;
        }
        if (n == 0) {
            // send()가 0을 반환하는 경우는 사실상 없지만
            // errno가 설정되지 않으므로 아래의 stale errno 판독을 막기 위해 명시적으로 처리
            return IoResult::error;
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return IoResult::would_block;
        }
        return IoResult::error;
    }
    return IoResult::ok;
}

} // namespace ddcs::ctrl::infra::frame
