#pragma once

#include <cstdint>

namespace ddcs::proto::msg {

enum class Type : std::uint8_t {
    RegisterRequest = 0x01,  // Agent -> Controller Request
    RegisterResponse = 0x02, // Controller -> Agent Response
    Heartbeat = 0x10,        // Agent -> Controller Notify
    Status = 0x11,           // Agent -> Controller Notify
    Command = 0x20,          // Controller -> Agent Command
    CommandAck = 0x21,       // Agent -> Controller CmdAck
    CommandOutcome = 0x22,   // Agent -> Controller CmdOut
};

} // namespace ddcs::proto::msg
