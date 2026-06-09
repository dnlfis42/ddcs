#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

class Outbound {
public:
    virtual ~Outbound() = default;

public:
    virtual common::PoolHandle<common::LinearBuffer> message_buffer() = 0;
    virtual void send(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> message) = 0;
    virtual void disconnect(ConnectionId id) = 0;
};

} // namespace ddcs::ctrl::app::agent::port
