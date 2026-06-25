#pragma once

#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/message_buffer.hpp"

namespace ddcs::ctrl::app::transport::port {

// 연결에서 받은 메시지의 통지를 받는 포트
class MessageReceiver {
public:
    virtual ~MessageReceiver() = default;

    virtual void on_message(ConnectionId id, MessageBuffer payload) = 0;
};

} // namespace ddcs::ctrl::app::transport::port
