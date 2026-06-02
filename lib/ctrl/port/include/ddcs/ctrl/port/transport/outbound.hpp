#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::port::transport {

// app 이 transport 에 종료를 요청하는 방식.
enum class CloseMode : std::uint8_t {
    graceful, // 남은 tx 드레인 후 FIN
    force,    // 즉시 RST
};

// outbound (driven) port: app -> infra(transport).
// infra 가 구현, app 이 호출. type 은 frame.type opaque 바이트(app 이 결정).
class Outbound {
public:
    virtual ~Outbound() = default;

    // frame header headroom 이 예약된 송신 버퍼 획득.
    virtual common::PoolHandle<common::LinearBuffer> payload_buffer() = 0;
    // body 를 type 으로 프레이밍해 송신. (payload_buffer() 로 받은 버퍼여야 headroom 보장.)
    virtual void send(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) = 0;
    virtual void close(ConnectionId id, CloseMode mode) = 0;
};

} // namespace ddcs::ctrl::port::transport
