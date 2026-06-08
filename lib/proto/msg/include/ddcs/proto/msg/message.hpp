#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ddcs::proto::msg {

enum class RegisterResult : std::uint8_t {
    success = 0,
    failed = 1,
};

enum class CommandResult : std::uint8_t {
    success = 0,
    failed = 1,
};

struct RegisterRequest {
    common::Uuid id;
    std::string group{};

    bool operator==(RegisterRequest const&) const = default;
};

struct RegisterResponse {
    RegisterResult result;
    std::string reason; // failed 시 진단용 사유. success 시 빈 문자열

    bool operator==(RegisterResponse const&) const = default;
};

struct Heartbeat {
    bool operator==(Heartbeat const&) const = default;
};

struct Status {
    std::uint8_t mode;
    double load;
    double temp;

    bool operator==(Status const&) const = default;
};

// 제네릭 명령 봉투. type/payload는 opaque - 의미(CommandType)는 proto::cmd 소유.
struct Command {
    std::uint64_t command_id;
    std::uint8_t type;   // opaque CommandType
    std::string payload; // opaque 명령 body (proto::cmd가 해석)

    bool operator==(Command const&) const = default;
};

struct CommandAck {
    std::uint64_t command_id;

    bool operator==(CommandAck const&) const = default;
};

struct CommandOutcome {
    std::uint64_t command_id;
    CommandResult result;
    std::string reason; // failed 시 진단용 사유. success 시 빈 문자열

    bool operator==(CommandOutcome const&) const = default;
};

// 메시지 구조체 -> frame 헤더의 type 바이트 매핑
template <typename T>
inline constexpr MessageType type_of = T::__type_specialization_missing;

template <>
inline constexpr MessageType type_of<RegisterRequest> = MessageType::register_request;

template <>
inline constexpr MessageType type_of<RegisterResponse> = MessageType::register_response;

template <>
inline constexpr MessageType type_of<Heartbeat> = MessageType::heartbeat;

template <>
inline constexpr MessageType type_of<Status> = MessageType::status;

template <>
inline constexpr MessageType type_of<Command> = MessageType::command;

template <>
inline constexpr MessageType type_of<CommandAck> = MessageType::command_ack;

template <>
inline constexpr MessageType type_of<CommandOutcome> = MessageType::command_outcome;

bool encode(RegisterRequest const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, RegisterRequest&);

bool encode(RegisterResponse const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, RegisterResponse&);

bool encode(Heartbeat const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, Heartbeat&) noexcept;

bool encode(Status const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, Status&) noexcept;

bool encode(Command const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, Command&);

bool encode(CommandAck const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, CommandAck&) noexcept;

bool encode(CommandOutcome const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, CommandOutcome&);

} // namespace ddcs::proto::msg
