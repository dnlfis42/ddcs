#pragma once

#include <cstdint>
#include <string_view>

namespace ddcs::agent::app::transport::port {

// 연결이 왜 끊겼는지. 이 값이 유일한 출처이므로 끊는 쪽이 이유를 정해 넘긴다.
enum class DisconnectReason : std::uint8_t {
    // transport가 판정
    connect_fail, // TCP 연결 자체가 성립하지 못함
    peer_closed,  // peer가 FIN 보냄
    io_error,     // kernel/network I/O 오류
    frame_error,  // frame 계층 오류(magic 불일치, 길이 초과, ring 손상)
    // app이 판정
    register_rejected,  // Controller가 등록을 거부함
    register_timeout,   // 판정을 기다리다 시한을 넘김
    bad_message,        // 디코딩 실패
    unexpected_message, // 그 상태에서 오지 않아야 하는 message type
    encode_fail,        // 송신 메시지 조립 실패(방어 경로)
};

// 로그/진단용 이름. 어휘 밖 값은 빈 문자열로 노출한다.
constexpr std::string_view to_string(DisconnectReason reason) noexcept {
    switch (reason) {
    case DisconnectReason::connect_fail:
        return "connect_fail";
    case DisconnectReason::peer_closed:
        return "peer_closed";
    case DisconnectReason::io_error:
        return "io_error";
    case DisconnectReason::frame_error:
        return "frame_error";
    case DisconnectReason::register_rejected:
        return "register_rejected";
    case DisconnectReason::register_timeout:
        return "register_timeout";
    case DisconnectReason::bad_message:
        return "bad_message";
    case DisconnectReason::unexpected_message:
        return "unexpected_message";
    case DisconnectReason::encode_fail:
        return "encode_fail";
    }
    return {};
}

} // namespace ddcs::agent::app::transport::port
