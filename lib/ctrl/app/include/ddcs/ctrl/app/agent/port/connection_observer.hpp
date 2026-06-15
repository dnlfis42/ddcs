#pragma once

#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"

namespace ddcs::ctrl::app::agent::port {

// 연결에서 일어나는 사건(수명 + 메시지)의 통지를 받는 관찰자
class ConnectionObserver {
public:
    virtual ~ConnectionObserver() = default;
    virtual void on_connected(ConnectionId id) = 0;
    // payload는 acmp 메시지 통째(`[type][body]`)다. type 디스패치는 수신측(app)이 peek_type로 한다.
    virtual void on_message(ConnectionId id, MessageBuffer payload) = 0;
    virtual void on_disconnected(ConnectionId id, DisconnectReason reason) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
