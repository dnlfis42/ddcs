#pragma once

#include <cstddef>
#include <cstdint>

namespace ddcs::agent::app::transport::port {

// app이 transport에게 예약하는 타이머 슬롯. transport는 의미를 모른다.
// reconnect backoff 타이머는 transport 내부 전용이라 여기 없다.
enum class TimerSlot : std::uint8_t {
    register_timeout, // registering: register_outcome 미수신 한도
    heartbeat,        // active: liveness 신호 주기
    status_report,           // active: 텔레메트리 주기
};

// 슬롯 수. transport가 슬롯별 예약 배열의 크기로 쓴다. 열거자를 추가하면 함께 갱신한다.
inline constexpr std::size_t timer_slot_count = 3;

} // namespace ddcs::agent::app::transport::port
