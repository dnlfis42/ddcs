#pragma once

#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

class Outbound {
public:
    virtual ~Outbound() = default;
    virtual MessageBuffer make_message_buffer() = 0;
    // best-effort
    virtual void send(ConnectionId id, std::uint8_t message_type, MessageBuffer message) = 0;
    // idempotent
    virtual void disconnect(ConnectionId id) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
