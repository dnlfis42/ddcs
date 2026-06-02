#pragma once

#include "ddcs/agent/port/timer_id.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <cstdint>

namespace ddcs::agent::port {

// inbound (driving) port: transport -> app. app 이 구현, transport 가 호출.
// 단일 연결이라 controller 측과 달리 id 인자가 없다. 재연결(backoff)은 transport 내부 책임.
// type 은 frame.type opaque 바이트(의미는 app 이 해석).
class Inbound {
public:
    virtual ~Inbound() = default;

    // TCP+epoll 등록 완료 - app 은 register 시작.
    virtual void on_connected() = 0;
    // 한 프레임 수신. body 는 frame.type 을 벗긴 payload.
    virtual void on_recv(std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) = 0;
    // 연결 끊김 - app 은 자기 FSM 을 idle 로 리셋.
    virtual void on_disconnected() = 0;
    // 예약된 타이머 만료.
    virtual void on_timer(TimerId id) = 0;
};

} // namespace ddcs::agent::port
