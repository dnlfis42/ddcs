#pragma once

#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

// 연결에서 일어나는 사건(수명 + 메시지)의 통지를 받는 관찰자
class ConnectionObserver {
public:
    virtual ~ConnectionObserver() = default;
    virtual void on_connected(ConnectionId id) = 0;
    virtual void on_message(ConnectionId id, std::uint8_t message_type, MessageBuffer body) = 0;
    virtual void on_disconnected(ConnectionId id, DisconnectReason reason) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
