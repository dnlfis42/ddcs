#pragma once

#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

// 연결로 메시지를 내보내는 송신 포트
class MessageSender {
public:
    virtual ~MessageSender() = default;
    virtual MessageBuffer make_message_buffer() = 0;
    // best-effort
    virtual void send(ConnectionId id, std::uint8_t message_type, MessageBuffer message) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
