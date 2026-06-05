#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::port::transport {

// transport가 감지해 app에게 통지하는 연결 종료 사유.
enum class DisconnectReason : std::uint8_t {
    local_drop,          // app이 drop 요청
    peer_closed,         // FIN 수신
    transport_error,     // RST 등 복구 불가한 소켓 오류
    protocol_error,      // framing/decode 검증 실패
    first_frame_timeout, // accept 뒤 첫 frame 미수신
};

// inbound (driving) port: infra(transport) -> app.
// app 이 구현, infra 가 호출. type 은 frame.type opaque 바이트(의미는 app 이 해석).
class Inbound {
public:
    virtual ~Inbound() = default;

    virtual void on_connected(ConnectionId id) = 0;
    virtual void on_recv(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) = 0;
    virtual void on_disconnecting(ConnectionId id, DisconnectReason reason) = 0;
    virtual void on_disconnected(ConnectionId id) = 0;
};

} // namespace ddcs::ctrl::port::transport
