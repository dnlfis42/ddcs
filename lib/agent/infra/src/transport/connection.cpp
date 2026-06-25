#include "ddcs/agent/infra/transport/connection.hpp"

#include "ddcs/agent/infra/transport/connector.hpp"
#include "ddcs/net/stream_io.hpp"

#include <cassert>
#include <utility>

namespace ddcs::agent::infra::transport {

namespace {

bool can_transition(Connection::State from, Connection::State to) noexcept {
    using S = Connection::State;
    switch (from) {
    case S::idle:
        return to == S::connecting;
    case S::connecting:
        return to == S::connected;
    case S::connected:
        return false; // 끊김은 reset()으로 직접 idle 화
    }
    return false;
}

} // namespace

// 정책 없음: Reactor가 알려온 readiness를 Connector로 곧장 위임.
void Connection::on_ready(io::Channel& channel, io::ChannelEvents events) {
    if (&channel != &channel_) {
        return;
    }
    connector_->on_connection_event(*this, events);
}

bool Connection::assign(io::Fd fd, io::ChannelEvents io_interest) noexcept {
    assert(state_ == State::idle && "assign() on non-idle connection");
    return channel_.init(std::move(fd), io_interest, *this);
}

bool Connection::transition(State to) noexcept {
    if (!can_transition(state_, to)) {
        return false;
    }
    state_ = to;
    return true;
}

void Connection::reset() noexcept {
    assert(!channel_.registered());
    channel_.close();
    state_ = State::idle;
    rx_buffer_.clear();
    while (!tx_queue_.empty()) {
        tx_queue_.pop();
    }
}

// ET drain 루프는 net::receive_into/transmit_from로 통일(ctrl transport와 공유).
// 전이는 하지 않고 결과만 보고한다.
Connection::IoResult Connection::receive() {
    return net::receive_into(channel_.fd(), rx_buffer_);
}

Connection::IoResult Connection::transmit() {
    return net::transmit_from(channel_.fd(), tx_queue_);
}

} // namespace ddcs::agent::infra::transport
