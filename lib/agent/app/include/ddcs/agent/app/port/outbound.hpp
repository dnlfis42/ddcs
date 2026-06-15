#pragma once

#include "ddcs/agent/app/port/timer_id.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <chrono>

namespace ddcs::agent::app::port {

// outbound (driven) port: app에서 transport 방향. transport가 구현, app이 호출.
// 단일 연결이라 id 인자가 없다.
class Outbound {
public:
    virtual ~Outbound() = default;

    // frame header headroom이 예약된 송신 버퍼 획득. encode 후 send()로 넘김.
    virtual common::PoolHandle<common::LinearBuffer> payload_buffer() = 0;
    // message를 프레이밍해 송신. message는 acmp 메시지 통째(`[type][body]`). (connected 아니면 드롭.)
    virtual void send(common::PoolHandle<common::LinearBuffer> message) = 0;
    // app 타이머: 같은 id의 기존 예약은 갈음(reschedule). delay 후 Inbound::on_timer(id).
    virtual void schedule_timer(TimerId id, std::chrono::nanoseconds delay) = 0;
    virtual void cancel_timer(TimerId id) = 0;
    // 현재 연결을 끊고 backoff 후 재연결 사이클로 진입. app 측 timeout/protocol_error 등에서 호출.
    virtual void close() = 0;
};

} // namespace ddcs::agent::app::port
