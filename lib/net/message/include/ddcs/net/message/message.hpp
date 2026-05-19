#pragma once

#include "ddcs/common/uuid.hpp"

#include <string>

#include <cstdint>

namespace ddcs::net::message {

enum class Opcode : std::uint8_t {
    RegisterRequest = 0x01,  // Agent -> Controller
    RegisterResponse = 0x02, // Controller -> Agent
    Heartbeat = 0x10,        // Agent -> Controller
    StatusUpdate = 0x20,     // Agent -> Controller
};

enum class RegisterResult : std::uint8_t {
    Success = 0,
    AlreadyConnected = 1,
};

enum class StatusMode : std::uint8_t {
    Safe = 0,
    Normal = 1,
    Performance = 2,
};

struct RegisterRequest {
    common::Uuid agent_tag;

    bool operator==(RegisterRequest const&) const = default;
};

struct RegisterResponse {
    RegisterResult result;
    std::string reason; // empty when Success

    bool operator==(RegisterResponse const&) const = default;
};

struct Heartbeat {
    std::uint64_t timestamp_ms;

    bool operator==(Heartbeat const&) const = default;
};

struct StatusUpdate {
    std::uint64_t timestamp_ms;
    StatusMode mode;
    // TBD: device_metrics, agent_metrics

    bool operator==(StatusUpdate const&) const = default;
};

} // namespace ddcs::net::message
