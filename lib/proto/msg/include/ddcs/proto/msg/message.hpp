#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <span>
#include <string>

#include <cstddef>
#include <cstdint>

namespace ddcs::proto::msg {

// 결과 코드 (일반 outcome - proto 계층에 둬도 무방).
enum class RegisterResult : std::uint8_t {
    success = 0,
    failed = 1,
};

enum class CommandResult : std::uint8_t {
    success = 0,
    failed = 1,
};

struct RegisterRequest {
    common::Uuid agent_uuid;
    std::string group{};   // 논리 그룹 라벨(정책 타깃팅). 빈 문자열 = 미지정
    std::string version{}; // agent 앱/빌드 버전(frame 의 프로토콜 버전과 별개)

    bool operator==(RegisterRequest const&) const = default;
};

struct RegisterResponse {
    RegisterResult result;
    std::string reason; // success 시 빈 문자열

    bool operator==(RegisterResponse const&) const = default;
};

struct Heartbeat {
    std::uint64_t timestamp_ms;

    bool operator==(Heartbeat const&) const = default;
};

struct Status {
    std::uint64_t timestamp_ms;
    std::string status_json; // 개방형 텔레메트리(JSON 유효성은 app 책임)

    bool operator==(Status const&) const = default;
};

// 제네릭 명령 봉투. type/payload 는 opaque - 의미(CommandType/SetMode)는 proto::cmd 소유.
struct Command {
    std::uint64_t command_id;
    std::uint8_t type;   // opaque CommandType
    std::string payload; // opaque 명령 body (proto::cmd 가 해석)

    bool operator==(Command const&) const = default;
};

struct CommandAck {
    std::uint64_t command_id;

    bool operator==(CommandAck const&) const = default;
};

struct CommandOutcome {
    std::uint64_t command_id;
    CommandResult result;
    std::string reason; // success 시 빈 문자열

    bool operator==(CommandOutcome const&) const = default;
};

// 타입 -> Type 매핑. 인코드 시 frame.type 결정에 사용(잘못된 T 는 컴파일 에러).
template <typename T>
inline constexpr Type type_of = T::__type_specialization_missing;
template <>
inline constexpr Type type_of<RegisterRequest> = Type::RegisterRequest;
template <>
inline constexpr Type type_of<RegisterResponse> = Type::RegisterResponse;
template <>
inline constexpr Type type_of<Heartbeat> = Type::Heartbeat;
template <>
inline constexpr Type type_of<Status> = Type::Status;
template <>
inline constexpr Type type_of<Command> = Type::Command;
template <>
inline constexpr Type type_of<CommandAck> = Type::CommandAck;
template <>
inline constexpr Type type_of<CommandOutcome> = Type::CommandOutcome;

// Wire format (body, frame.type 바이트 미포함):
//   정수      : little-endian
//   Uuid      : raw 16 byte
//   string    : u16le 길이 + UTF-8 (단, Command.payload 는 길이 prefix 없이 body 의 나머지 전부)
//   enum class: underlying type raw (1 byte)
// encode: LinearBuffer 부족 시 false. decode: 길이 부족/trailing bytes 시 false (strict).

bool encode(RegisterRequest const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, RegisterRequest&); // read_string -> noexcept 아님

bool encode(RegisterResponse const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, RegisterResponse&);

bool encode(Heartbeat const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, Heartbeat&) noexcept;

bool encode(Status const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, Status&);

bool encode(Command const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, Command&);

bool encode(CommandAck const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, CommandAck&) noexcept;

bool encode(CommandOutcome const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, CommandOutcome&);

} // namespace ddcs::proto::msg
