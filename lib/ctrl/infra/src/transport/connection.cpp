#include "ddcs/ctrl/infra/transport/connection.hpp"

#include "ddcs/ctrl/infra/transport/server.hpp"
#include "ddcs/net/stream_io.hpp"

#include <cassert>
#include <utility>

namespace ddcs::ctrl::infra::transport {

void Connection::init(
    Server& server, port::ConnectionId id, PeerAddress peer, io::Fd&& fd,
    io::ChannelEvents interests
) noexcept {
    channel_.init(std::move(fd), interests, *this);

    server_ = &server;
    id_ = id;
    peer_ = peer;
}

void Connection::close() noexcept {
    assert(!channel_.registered());

    server_ = nullptr;
    id_.clear();
    peer_.clear();
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

    server_->on_connection_event(*this, events);
}

// ET drain 루프는 net::receive_into/transmit_from로 통일(agent transport와 공유).
// registered 확인은 호출부(Server) 책임이므로 여기 남긴다.
net::ReceiveResult Connection::receive() {
    assert(channel_.registered());
    return net::receive_into(channel_.fd(), rx_buffer_);
}

net::TransmitResult Connection::transmit() {
    assert(channel_.registered());
    return net::transmit_from(channel_.fd(), tx_queue_);
}

} // namespace ddcs::ctrl::infra::transport
