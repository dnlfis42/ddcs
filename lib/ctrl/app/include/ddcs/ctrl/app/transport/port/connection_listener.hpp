#pragma once

#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/disconnect_reason.hpp"

namespace ddcs::ctrl::app::transport::port {

// 연결 수명 사건(연결/해제)의 통지를 받는 포트
class ConnectionListener {
public:
    virtual ~ConnectionListener() = default;

    virtual void on_connected(ConnectionId id) = 0;
    virtual void on_disconnected(ConnectionId id, DisconnectReason reason) = 0;
};

} // namespace ddcs::ctrl::app::transport::port
