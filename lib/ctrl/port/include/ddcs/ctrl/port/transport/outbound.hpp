#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::port::transport {

// outbound (driven) port: app -> infra(transport).
// infra 가 구현, app 이 호출. type 은 frame.type opaque 바이트(app 이 결정).
class Outbound {
public:
    virtual ~Outbound() = default;

    // send()에 넘길 송신 body 버퍼를 획득한다.
    virtual common::PoolHandle<common::LinearBuffer> send_buffer() = 0;
    // body를 type으로 프레이밍해 송신한다.
    virtual void send(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) = 0;
    virtual void drop(ConnectionId id) = 0;
};

} // namespace ddcs::ctrl::port::transport
