#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::port::transport {

// transport 가 감지해 app 에게 통지하는 종료 사유.
// 항상 on_close_request 뒤에 app 의 Outbound::close() 가 따른다.
enum class CloseReason : std::uint8_t {
    peer_closed,    // FIN 수신 (정상 half-close)
    conn_error,     // RST 등 복구 불가한 소켓 오류
    protocol_error, // framing/decode 검증 실패
};

// inbound (driving) port: infra(transport) -> app.
// app 이 구현, infra 가 호출. type 은 frame.type opaque 바이트(의미는 app 이 해석).
class Inbound {
public:
    virtual ~Inbound() = default;

    virtual void on_connect(ConnectionId id) = 0;
    virtual void on_recv(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) = 0;
    virtual void on_close_request(ConnectionId id, CloseReason reason) = 0;
    virtual void on_disconnect(ConnectionId id) = 0;
};

} // namespace ddcs::ctrl::port::transport
