#pragma once

#include <cstdint>

namespace ddcs::dacp::msg {

// CAUTION: 값은 DACP frame header의 type 바이트와 wire-compatible해야 한다.
enum class MessageType : std::uint8_t {
    register_request = 0x01,  // Agent -> Controller 등록 요청
    register_response = 0x02, // Controller -> Agent 등록 응답
    heartbeat = 0x10,         // Agent -> Controller keepalive
    status = 0x11,            // Agent -> Controller 상태 보고
    command = 0x20,           // Controller -> Agent 명령
    command_ack = 0x21,       // Agent -> Controller 명령 수신 확인
    command_outcome = 0x22,   // Agent -> Controller 명령 처리 결과
};

} // namespace ddcs::dacp::msg
