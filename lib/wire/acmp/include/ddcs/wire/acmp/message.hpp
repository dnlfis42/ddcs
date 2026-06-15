#pragma once

#include "ddcs/common/uuid.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace ddcs::wire::acmp {

inline constexpr std::size_t max_string_field_size{std::numeric_limits<std::uint16_t>::max()};

enum class MessageType : std::uint8_t {
    invalid = 0x00,          // 유효하지 않은 값
    register_request = 0x01, // Agent가 Controller로 보내는 등록 요청
    register_outcome = 0x02, // Controller가 Agent로 보내는 등록 처리 결과
    register_ack = 0x03,     // Agent가 Controller로 보내는 결과 수신 확인
    heartbeat = 0x10,        // Agent가 Controller로 보내는 keepalive
    status = 0x11,           // Agent가 Controller로 보내는 상태 보고
    command_request = 0x20,  // Controller가 Agent로 보내는 명령
    command_ack = 0x21,      // Agent가 Controller로 보내는 명령 수신 확인
    command_outcome = 0x22,  // Agent가 Controller로 보내는 명령 수행 결과
};

// command_request 헤더(`[type][command_id(u64le)][command_type(u8)]`)의 wire 크기.
// payload 앞에 헤더를 제자리 prepend하는 송신 경로의 headroom 요구량이다.
inline constexpr std::size_t command_request_header_size{
    sizeof(MessageType) + sizeof(std::uint64_t) + sizeof(std::uint8_t)
};

// CAUTION: string_view/span 멤버는 decode에 넘긴 바이트(buffer)가 살아있는 동안에만 유효하다.

struct RegisterRequest {
    common::Uuid id;
    std::string_view group;
};

struct RegisterOutcome {
    std::uint8_t code;
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
    std::uint64_t command_id;
    std::uint8_t code;
};

// codec은 순수 span 변환이다. 버퍼 커서(commit/consume)는 호출측(infra)이 관리한다.

// frame payload 첫 바이트의 message type을 읽는다(소비하지 않음).
// CAUTION: 값 유효성은 검증하지 않는다. 빈 span이면 invalid를 돌려준다(empty/0x00 모두 invalid).
[[nodiscard]] MessageType peek_type(std::span<std::byte const> in) noexcept;

// encode_<X>: out에 [type][body]를 forward로 기록하고 쓴 바이트 수를 반환한다(공간 부족이면 nullopt).
// decode_<X>: in(= type 이후 body)에서 메시지를 푼다. 구조 불일치(부족/trailing)면 nullopt.
//             결과의 view 멤버는 in을 차용한다.

[[nodiscard]] std::optional<std::size_t>
encode_register_request(common::Uuid const& id, std::string_view group, std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<RegisterRequest> decode_register_request(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_register_outcome(std::uint8_t code, std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<RegisterOutcome> decode_register_outcome(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_register_ack(std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<RegisterAck> decode_register_ack(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t> encode_heartbeat(std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<Heartbeat> decode_heartbeat(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_status(std::uint8_t mode, double load, double temp, std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<Status> decode_status(std::span<std::byte const> in) noexcept;

// payload는 호출측이 out 뒤에 직접 append한다(예: device가 command body 인코딩). 여기선 header만 쓴다.
[[nodiscard]] std::optional<std::size_t>
encode_command_request_header(std::uint64_t command_id, std::uint8_t command_type, std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<CommandRequest> decode_command_request(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_command_ack(std::uint64_t command_id, std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<CommandAck> decode_command_ack(std::span<std::byte const> in) noexcept;

[[nodiscard]] std::optional<std::size_t>
encode_command_outcome(std::uint64_t command_id, std::uint8_t code, std::span<std::byte> out) noexcept;
[[nodiscard]] std::optional<CommandOutcome> decode_command_outcome(std::span<std::byte const> in) noexcept;

} // namespace ddcs::wire::acmp
