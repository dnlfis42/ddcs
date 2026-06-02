#pragma once

#include "ddcs/agent/port/timer_id.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <chrono>

#include <cstdint>

namespace ddcs::agent::port {

// outbound (driven) port: app -> transport. transport 가 구현, app 이 호출.
// 단일 연결이라 id 인자가 없다. type 은 frame.type opaque 바이트(app 이 결정).
class Outbound {
public:
    virtual ~Outbound() = default;

    // frame header headroom 이 예약된 송신 버퍼 획득. encode 후 send() 로 넘김.
    virtual common::PoolHandle<common::LinearBuffer> payload_buffer() = 0;
    // body 를 type 으로 프레이밍해 송신. (connected 아니면 드롭.)
    virtual void send(std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) = 0;
    // app 타이머: 같은 id 의 기존 예약은 갈음(reschedule). delay 후 Inbound::on_timer(id).
    virtual void schedule_timer(TimerId id, std::chrono::nanoseconds delay) = 0;
    virtual void cancel_timer(TimerId id) = 0;
    // 현재 연결을 끊고 backoff 후 재연결 사이클로 진입. app 측 timeout/protocol_error 등에서 호출.
    virtual void close() = 0;
};

} // namespace ddcs::agent::port
