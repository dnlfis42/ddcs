#pragma once

#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

class Inbound {
public:
    virtual ~Inbound() = default;
    virtual void on_connected(ConnectionId id) = 0;
    virtual void on_message(ConnectionId id, std::uint8_t message_type, MessageBuffer body) = 0;
    virtual void on_disconnected(ConnectionId id, DisconnectReason reason) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
