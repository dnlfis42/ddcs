#pragma once

#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"

namespace ddcs::ctrl::app::agent::port {

// 연결로 메시지를 내보내는 송신 포트
class MessageSender {
public:
    virtual ~MessageSender() = default;
    virtual MessageBuffer make_message_buffer() = 0;
    // best-effort. message는 acmp 메시지 통째(`[type][body]`). infra는 frame header만 덧씌운다.
    virtual void send(ConnectionId id, MessageBuffer message) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
