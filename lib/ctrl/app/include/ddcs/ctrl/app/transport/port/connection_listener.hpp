#pragma once

#include "ddcs/ctrl/app/transport/port/connection_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ddcs::ctrl::app::transport::port {

// 세션이 왜 끝났는지. 이 값이 유일한 출처이므로 끊는 쪽이 이유를 정해 넘긴다.
enum class DisconnectReason : std::uint8_t {
    // transport가 판정
    peer_closed, // peer가 FIN 보냄
    io_error,    // kernel/network I/O 오류
    frame_error, // frame 계층 오류(magic 불일치, 길이 초과, ring 손상)
    // app이 판정
    shutdown,           // 프로세스 종료
    kicked,             // 같은 device의 새 세션이 밀어냄(new-wins)
    handshake_expired,  // 등록을 제 시간에 못 끝냄
    liveness_expired,   // active인데 침묵
    register_rejected,  // 등록을 거부함
    bad_message,        // 디코딩 실패
    unexpected_message, // 그 상태에서 오지 않아야 하는 message type
    internal_error,     // 방어 경로(정상 배선에서는 닿지 않음)
    count,
};

inline constexpr std::size_t disconnect_reason_count =
    static_cast<std::size_t>(DisconnectReason::count);

inline constexpr std::array<DisconnectReason, disconnect_reason_count> disconnect_reasons{
    DisconnectReason::peer_closed,      DisconnectReason::io_error,
    DisconnectReason::frame_error,      DisconnectReason::shutdown,
    DisconnectReason::kicked,           DisconnectReason::handshake_expired,
    DisconnectReason::liveness_expired, DisconnectReason::register_rejected,
    DisconnectReason::bad_message,      DisconnectReason::unexpected_message,
    DisconnectReason::internal_error,
};

constexpr std::size_t disconnect_reason_index(DisconnectReason reason) noexcept {
    return static_cast<std::size_t>(reason);
}

// 로그/진단용 이름. 어휘 밖 값은 빈 문자열로 노출한다.
constexpr std::string_view to_string(DisconnectReason reason) noexcept {
    switch (reason) {
    case DisconnectReason::peer_closed:
        return "peer_closed";
    case DisconnectReason::io_error:
        return "io_error";
    case DisconnectReason::frame_error:
        return "frame_error";
    case DisconnectReason::shutdown:
        return "shutdown";
    case DisconnectReason::kicked:
        return "kicked";
    case DisconnectReason::handshake_expired:
        return "handshake_expired";
    case DisconnectReason::liveness_expired:
        return "liveness_expired";
    case DisconnectReason::register_rejected:
        return "register_rejected";
    case DisconnectReason::bad_message:
        return "bad_message";
    case DisconnectReason::unexpected_message:
        return "unexpected_message";
    case DisconnectReason::internal_error:
        return "internal_error";
    case DisconnectReason::count:
        break;
    }
    return {};
}

// 연결 수명 사건(연결/해제)의 통지를 받는 포트
class ConnectionListener {
public:
    virtual ~ConnectionListener() = default;

    virtual void on_connected(ConnectionId id) = 0;
    virtual void on_disconnected(ConnectionId id, DisconnectReason reason) = 0;
};

} // namespace ddcs::ctrl::app::transport::port
