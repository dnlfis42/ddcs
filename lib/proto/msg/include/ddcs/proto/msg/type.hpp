#pragma once

#include <cstdint>

namespace ddcs::proto::msg {

// frame 헤더의 type 바이트에 실리는 메시지 종류
enum class MessageType : std::uint8_t {
    register_request = 0x01,  // agent -> controller 등록 요청
    register_response = 0x02, // controller -> agent 등록 응답
    heartbeat = 0x10,         // agent -> controller keepalive
    status = 0x11,            // agent -> controller 상태 보고
    command = 0x20,           // controller -> agent 명령
    command_ack = 0x21,       // agent -> controller 명령 수신 확인
    command_outcome = 0x22,   // agent -> controller 명령 처리 결과
};

} // namespace ddcs::proto::msg
