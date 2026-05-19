#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ddcs::net::message {

enum class Opcode : std::uint8_t {
    RegisterRequest = 0x10, // Agent -> Controller
    RegisterSuccess = 0x11, // Controller -> Agent
    RegisterFail = 0x12,    // Controller -> Agent
    Status = 0x20,          // Agent -> Controller
    Command = 0x30,         // Controller -> Agent
    CommandAck = 0x31,      // Agent -> Controller
    CommandSuccess = 0x32,  // Agent -> Controller
    CommandFail = 0x33,     // Agent -> Controller
};

struct RegisterRequest {
    std::string agent_tag;

    bool operator==(RegisterRequest const&) const = default;
};

struct RegisterSuccess {
    bool operator==(RegisterSuccess const&) const = default;
};

struct RegisterFail {
    std::string reason;

    bool operator==(RegisterFail const&) const = default;
};

struct Status {
    std::uint64_t timestamp_ns;
    std::string state;

    bool operator==(Status const&) const = default;
};

struct Command {
    std::uint64_t command_id;
    std::string body;

    bool operator==(Command const&) const = default;
};

struct CommandAck {
    std::uint64_t command_id;

    bool operator==(CommandAck const&) const = default;
};

struct CommandSuccess {
    std::uint64_t command_id;

    bool operator==(CommandSuccess const&) const = default;
};

struct CommandFail {
    std::uint64_t command_id;
    std::string reason;

    bool operator==(CommandFail const&) const = default;
};

std::vector<std::byte> encode(RegisterRequest const& msg);
std::vector<std::byte> encode(RegisterSuccess const& msg);
std::vector<std::byte> encode(RegisterFail const& msg);
std::vector<std::byte> encode(Status const& msg);
std::vector<std::byte> encode(Command const& msg);
std::vector<std::byte> encode(CommandAck const& msg);
std::vector<std::byte> encode(CommandSuccess const& msg);
std::vector<std::byte> encode(CommandFail const& msg);

std::optional<RegisterRequest> decode_register_request(std::span<std::byte const> src);
std::optional<RegisterSuccess> decode_register_success(std::span<std::byte const> src);
std::optional<RegisterFail> decode_register_fail(std::span<std::byte const> src);
std::optional<Status> decode_status(std::span<std::byte const> src);
std::optional<Command> decode_command(std::span<std::byte const> src);
std::optional<CommandAck> decode_command_ack(std::span<std::byte const> src);
std::optional<CommandSuccess> decode_command_success(std::span<std::byte const> src);
std::optional<CommandFail> decode_command_fail(std::span<std::byte const> src);

} // namespace ddcs::net::message
