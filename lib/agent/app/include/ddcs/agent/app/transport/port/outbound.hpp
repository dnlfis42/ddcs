#pragma once

#include "ddcs/agent/app/transport/port/message_buffer.hpp"
#include "ddcs/agent/app/transport/port/timer_slot.hpp"

#include <chrono>

namespace ddcs::agent::app::transport::port {

// outbound (driven) port
// - app에서 transport 방향. transport가 구현, app이 호출
class Outbound {
public:
    virtual ~Outbound() = default;

    // frame header headroom이 예약된 송신 버퍼 획득. encode 후 send()로 넘긴다.
    virtual MessageBuffer payload_buffer() = 0;
    // message를 프레이밍해 송신한다.
    // - connected 아니면 드롭
    virtual void send(MessageBuffer message) = 0;
    // app 타이머: 같은 id의 기존 예약은 갈음(reschedule). delay 후 Inbound::on_timer(id)
    virtual void schedule_timer(TimerSlot id, std::chrono::nanoseconds delay) = 0;
    virtual void cancel_timer(TimerSlot id) = 0;
    // 현재 연결을 끊고 backoff 후 재연결 사이클로 진입. app 측 timeout/protocol_error 등에서 호출
    virtual void close() = 0;
    // app 레벨 등록(handshake) 성공을 transport에 알린다.
    // transport는 이때 reconnect backoff를 base로 리셋한다.
    //
    // TCP 연결만으로 리셋하지 않는 이유:
    // - controller가 TCP는 accept하지만 등록을 끝내지 못하는 상태에서 끊김
    // - 사이클이 반복될 때 backoff가 지수적으로 자라도록 하기 위함.
    virtual void notify_registered() = 0;
};

} // namespace ddcs::agent::app::transport::port
