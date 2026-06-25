#pragma once

#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/message_buffer.hpp"

namespace ddcs::ctrl::app::transport::port {

// 연결로 메시지를 내보내는 송신 포트
class MessageSender {
public:
    virtual ~MessageSender() = default;

    virtual MessageBuffer make_message_buffer() = 0;
    virtual void send(ConnectionId id, MessageBuffer message) = 0;
};

} // namespace ddcs::ctrl::app::transport::port
