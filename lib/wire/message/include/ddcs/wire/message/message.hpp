#pragma once

#include "ddcs/common/uuid.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace ddcs::wire::message {

enum class MessageType : std::uint8_t {
    invalid = 0x00,
    register_request = 0x01, // Agent가 Controller로 보내는 등록 요청
    register_outcome = 0x02, // Controller가 Agent로 보내는 등록 처리 결과
    register_ack = 0x03,     // Agent가 Controller로 보내는 결과 수신 확인
    heartbeat = 0x10,        // Agent가 Controller로 보내는 keepalive
    status_report = 0x11,    // Agent가 Controller로 보내는 상태 보고
    command_request = 0x20,  // Controller가 Agent로 보내는 명령
    command_ack = 0x21,      // Agent가 Controller로 보내는 명령 수신 확인
    command_outcome = 0x22,  // Agent가 Controller로 보내는 명령 수행 결과
};

// 로그/진단용 이름. 어휘 밖 값(구버전/손상)은 빈 문자열로 노출한다.
constexpr std::string_view to_string(MessageType type) noexcept {
    switch (type) {
    case MessageType::invalid:
        return "invalid";
    case MessageType::register_request:
        return "register_request";
    case MessageType::register_outcome:
        return "register_outcome";
    case MessageType::register_ack:
        return "register_ack";
    case MessageType::heartbeat:
        return "heartbeat";
    case MessageType::status_report:
        return "status_report";
    case MessageType::command_request:
        return "command_request";
    case MessageType::command_ack:
        return "command_ack";
    case MessageType::command_outcome:
        return "command_outcome";
    }
    return {};
}

// CAUTION: string_view/span 멤버는 decode에 넘긴 바이트(buffer)가 살아있는 동안에만 유효하다.

struct RegisterRequest {
    common::Uuid uuid;
    std::string_view group;
};

struct RegisterOutcome {
    enum class Code : std::uint8_t { success = 0, failed = 1 };

    Code code;
};

// 로그/진단용 이름. 어휘 밖 값(구버전/손상)은 빈 문자열로 노출한다.
constexpr std::string_view to_string(RegisterOutcome::Code code) noexcept {
    switch (code) {
    case RegisterOutcome::Code::success:
        return "success";
    case RegisterOutcome::Code::failed:
        return "failed";
    }
    return {};
}

struct RegisterAck {};

struct Heartbeat {};

struct StatusReport {
    std::uint8_t mode;
    double load;
    double temp;
};

struct CommandRequest {
    std::uint64_t command_id;
    std::uint8_t command_type;
    std::span<std::byte const> payload;
};

struct CommandAck {
    std::uint64_t command_id;
};

struct CommandOutcome {
    // 실패 사유는 Agent만 알기에 code로 실어 Controller의 로그와 정책이 쓸 수 있게 한다.
    // failed는 아래 사유로 갈리지 않는 실패를 담는 자리이다.
    enum class Code : std::uint8_t {
        success = 0,
        failed = 1,
        apply_failed = 2, // device.apply()가 거부
        bad_mode = 3,     // mode 어휘 밖의 wire byte
        bad_payload = 4,  // command body 디코딩 실패
        unknown_type = 5, // 미지 command_type
    };

    std::uint64_t command_id;
    Code code;
};

// 로그/진단용 이름. 어휘 밖 값(구버전/손상)은 빈 문자열로 노출한다.
constexpr std::string_view to_string(CommandOutcome::Code code) noexcept {
    switch (code) {
    case CommandOutcome::Code::success:
        return "success";
    case CommandOutcome::Code::failed:
        return "failed";
    case CommandOutcome::Code::apply_failed:
        return "apply_failed";
    case CommandOutcome::Code::bad_mode:
        return "bad_mode";
    case CommandOutcome::Code::bad_payload:
        return "bad_payload";
    case CommandOutcome::Code::unknown_type:
        return "unknown_type";
    }
    return {};
}

// frame payload 선두의 message type을 읽는다(검증/소비 없음). 빈 span이면 invalid 반환
[[nodiscard]] MessageType message_type(std::span<std::byte const> in) noexcept;

// encode_<X>: out에 [type][body]를 forward로 쓰고 쓴 바이트 수 반환. 공간 부족이면 nullopt

[[nodiscard]] std::optional<std::size_t> encode_register_request(
    std::span<std::byte> out, common::Uuid const& uuid, std::string_view group
) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_register_outcome(std::span<std::byte> out, RegisterOutcome::Code code) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_register_ack(std::span<std::byte> out) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_heartbeat(std::span<std::byte> out) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_status_report(
    std::span<std::byte> out, std::uint8_t mode, double load, double temp
) noexcept;

/// @brief `[type][command_id(u64le)][command_type(u8)]`의 크기
inline constexpr std::size_t command_request_header_size =
    sizeof(MessageType) + sizeof(std::uint64_t) + sizeof(std::uint8_t);

// payload는 호출측이 out 뒤에 직접 append한다. 여기선 header만 쓴다.
[[nodiscard]] std::optional<std::size_t> encode_command_request_header(
    std::span<std::byte> out, std::uint64_t command_id, std::uint8_t command_type
) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_command_ack(std::span<std::byte> out, std::uint64_t command_id) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_command_outcome(
    std::span<std::byte> out, std::uint64_t command_id, CommandOutcome::Code code
) noexcept;

/// @brief 수신한 메시지의 전체 어휘
using Message = std::variant<
    RegisterRequest, RegisterOutcome, RegisterAck, Heartbeat, StatusReport, CommandRequest,
    CommandAck, CommandOutcome>;

/// @brief Message를 디코딩한다.
///
/// @param in Message가 들어있는 payload
///
/// @retval Message 정상적인 Message인 경우
/// @retval nullopt 빈 payload, 미지 type, 구조 불일치인 경우
[[nodiscard]] std::optional<Message> decode_message(std::span<std::byte const> in) noexcept;

} // namespace ddcs::wire::message
