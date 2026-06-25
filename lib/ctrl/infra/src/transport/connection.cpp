#include "ddcs/ctrl/infra/transport/connection.hpp"

#include "ddcs/ctrl/infra/transport/server.hpp"
#include "ddcs/net/stream_io.hpp"

#include <cassert>
#include <utility>

namespace ddcs::ctrl::infra::transport {

bool Connection::init(
    Server& server, port::ConnectionId id, PeerAddress peer, io::Fd&& fd,
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

void Connection::reset() noexcept {
    close();
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

// ET drain 루프는 net::receive_into/transmit_from로 통일(agent transport와 공유).
// active 상태 확인은 호출부(Server) 책임이므로 여기 남긴다.
Connection::IoResult Connection::receive() {
    assert(state_ == State::active);
    return net::receive_into(channel_.fd(), rx_buffer_);
}

Connection::IoResult Connection::transmit() {
    assert(state_ == State::active);
    return net::transmit_from(channel_.fd(), tx_queue_);
}

} // namespace ddcs::ctrl::infra::transport
