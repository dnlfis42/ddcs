#pragma once

#include "ddcs/common/uuid.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ddcs::wire::acmp {

enum class MessageType : std::uint8_t {
    invalid = 0x00,
    register_request = 0x01, // Agent가 Controller로 보내는 등록 요청
    register_outcome = 0x02, // Controller가 Agent로 보내는 등록 처리 결과
    register_ack = 0x03,     // Agent가 Controller로 보내는 결과 수신 확인
    heartbeat = 0x10,        // Agent가 Controller로 보내는 keepalive
    status = 0x11,           // Agent가 Controller로 보내는 상태 보고
    command_request = 0x20,  // Controller가 Agent로 보내는 명령
    command_ack = 0x21,      // Agent가 Controller로 보내는 명령 수신 확인
    command_outcome = 0x22,  // Agent가 Controller로 보내는 명령 수행 결과
};

// CAUTION: string_view/span 멤버는 decode에 넘긴 바이트(buffer)가 살아있는 동안에만 유효하다.

struct RegisterRequest {
    common::Uuid id;
    std::string_view group;
};

struct RegisterOutcome {
    enum class Code : std::uint8_t { success = 0, failed = 1 };

    Code code;
};

struct RegisterAck {};

struct Heartbeat {};

struct Status {
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
    enum class Code : std::uint8_t { success = 0, failed = 1 };

    std::uint64_t command_id;
    Code code;
};

// 프레임 payload 선두의 message type을 읽는다(검증/소비 없음). 빈 span이면 invalid.
[[nodiscard]] MessageType message_type(std::span<std::byte const> in) noexcept;

// encode_<X>: out에 [type][body]를 forward로 쓰고 쓴 바이트 수 반환. 공간 부족이면 nullopt
// decode_<X>: in(type 이후 body)을 푼다. 구조 불일치면 nullopt. view 멤버는 in을 차용

[[nodiscard]] std::optional<std::size_t> encode_register_request(
    std::span<std::byte> out, common::Uuid const& id, std::string_view group
) noexcept;
[[nodiscard]] std::optional<RegisterRequest>
decode_register_request(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_register_outcome(std::span<std::byte> out, RegisterOutcome::Code code) noexcept;
[[nodiscard]] std::optional<RegisterOutcome>
decode_register_outcome(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_register_ack(std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<RegisterAck>
decode_register_ack(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_heartbeat(std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<Heartbeat> decode_heartbeat(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_status(std::span<std::byte> out, std::uint8_t mode, double load, double temp) noexcept;
[[nodiscard]] std::optional<Status> decode_status(std::span<std::byte const> in) noexcept;

// command_request header wire size: [type][command_id(u64 le)][command_type(u8)]
// 송신 경로가 payload 앞에 이 header를 제자리 prepend할 headroom으로 쓴다.
inline constexpr std::size_t command_request_header_size{
    sizeof(MessageType) + sizeof(std::uint64_t) + sizeof(std::uint8_t)
};

// payload는 호출측이 out 뒤에 직접 append한다. 여기선 header만 쓴다.
[[nodiscard]] std::optional<std::size_t> encode_command_request_header(
    std::span<std::byte> out, std::uint64_t command_id, std::uint8_t command_type
) noexcept;
[[nodiscard]] std::optional<CommandRequest>
decode_command_request(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_command_ack(std::span<std::byte> out, std::uint64_t command_id) noexcept;
[[nodiscard]] std::optional<CommandAck> decode_command_ack(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_command_outcome(
    std::span<std::byte> out, std::uint64_t command_id, CommandOutcome::Code code
) noexcept;
[[nodiscard]] std::optional<CommandOutcome>
decode_command_outcome(std::span<std::byte const> in) noexcept;

} // namespace ddcs::wire::acmp
