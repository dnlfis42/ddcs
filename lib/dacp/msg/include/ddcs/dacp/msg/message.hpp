#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/dacp/msg/type.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ddcs::dacp::msg {

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
    std::string reason; // success면 비어 있음

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

// DACP는 type과 payload를 opaque command bytes로 전달한다.
struct Command {
    std::uint64_t command_id;
    std::uint8_t type;

    bool operator==(Command const&) const = default;
};

struct CommandAck {
    std::uint64_t command_id;

    bool operator==(CommandAck const&) const = default;
};

struct CommandOutcome {
    std::uint64_t command_id;
    CommandResult result;
    std::string reason; // success면 비어 있음

    bool operator==(CommandOutcome const&) const = default;
};

// message 구조체 -> DACP frame type 매핑
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

bool encode(Command const&, std::span<std::byte const> payload, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, Command&, std::span<std::byte const>& payload) noexcept;

bool encode(CommandAck const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, CommandAck&) noexcept;

bool encode(CommandOutcome const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, CommandOutcome&);

} // namespace ddcs::dacp::msg
