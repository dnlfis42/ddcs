#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

class Inbound {
public:
    virtual ~Inbound() = default;

public:
    virtual void on_connected(ConnectionId id) = 0;
    virtual void on_message(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) = 0;
    virtual void on_disconnecting(ConnectionId id, DisconnectReason reason) = 0;
    virtual void on_disconnected(ConnectionId id) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
