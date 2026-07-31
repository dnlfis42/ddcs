#pragma once

#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/connection_listener.hpp"

namespace ddcs::ctrl::app::transport::port {

// 연결 강제 종료 포트 (kick/evict 경로)
class Disconnector {
public:
    virtual ~Disconnector() = default;

    // CAUTION: 동기로 on_disconnected를 되부른다. 순회 중 호출 금지, 호출 뒤 관련 포인터 재조회
    virtual void disconnect(ConnectionId id, DisconnectReason reason) = 0;
};

} // namespace ddcs::ctrl::app::transport::port
