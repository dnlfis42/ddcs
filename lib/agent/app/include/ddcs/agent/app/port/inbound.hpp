#pragma once

#include "ddcs/agent/app/port/timer_id.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

namespace ddcs::agent::app::port {

// inbound (driving) port: transport에서 app 방향. app이 구현, transport가 호출
// 단일 연결이라 controller 측과 달리 id 인자가 없다. 재연결(backoff)은 transport 내부 책임
class Inbound {
public:
    virtual ~Inbound() = default;

    // TCP+epoll 등록이 끝나면 app은 register 시작
    virtual void on_connected() = 0;
    // 한 프레임 수신. payload는 acmp 메시지 (`[type][body]`)
    virtual void on_recv(common::PoolHandle<common::LinearBuffer> payload) = 0;
    // 연결이 끊기면 app은 자기 FSM을 idle로 리셋
    virtual void on_disconnected() = 0;
    // 예약된 타이머 만료
    virtual void on_timer(TimerId id) = 0;
};

} // namespace ddcs::agent::app::port
