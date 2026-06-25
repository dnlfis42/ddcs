#pragma once

#include <cstdint>

namespace ddcs::agent::app::transport::port {

// app이 transport에게 예약하는 타이머 슬롯(고정 3종)
// - transport는 의미를 모른다.
// - reconnect backoff 타이머는 transport 내부 전용이라 여기 없다.
enum class TimerSlot : std::uint8_t {
    register_timeout, // registering: register_outcome 미수신 한도
    heartbeat,        // active: liveness 신호 주기
    status,           // active: 텔레메트리 주기
};

} // namespace ddcs::agent::app::transport::port
