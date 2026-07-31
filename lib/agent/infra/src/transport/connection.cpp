#include "ddcs/agent/infra/transport/connection.hpp"

#include "ddcs/agent/infra/transport/connector.hpp"
#include "ddcs/logger/event.hpp"

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
        return false; // 끊김은 close()로 직접 idle 화
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

void Connection::init(Connector& connector, io::Fd fd, io::ChannelEvents io_interest) noexcept {
    assert(state_ == State::idle && "init() on non-idle connection");
    connector_ = &connector;
    channel_.init(std::move(fd), io_interest, *this);
}

void Connection::transition(State to) noexcept {
    if (!can_transition(state_, to)) {
        // 조용히 지나가면 FSM 어긋남의 원인 추적이 불가능해진다
        LOG_TRANSPORT_TRANSITION_INVALID(to_string(state_), to_string(to));
        return;
    }
    state_ = to;
}

void Connection::close() noexcept {
    assert(!channel_.registered());
    channel_.close();
    state_ = State::idle;
    rx_buffer_.clear();
    while (!tx_queue_.empty()) {
        tx_queue_.pop();
    }
}

} // namespace ddcs::agent::infra::transport
