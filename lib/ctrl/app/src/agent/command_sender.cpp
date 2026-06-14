#include "ddcs/ctrl/app/agent/command_sender.hpp"

#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/dacp/msg/message.hpp"
#include "ddcs/logger/log.hpp"

#include <cassert>
#include <cstdint>
#include <utility>

namespace ddcs::ctrl::app::agent {

namespace msg = ddcs::dacp::msg;

CommandSender::CommandSender(AgentRegistry& agents, port::MessageSender& sender) noexcept
    : agents_{agents}, sender_{sender} {}

device::port::CommandBuffer CommandSender::make_command_buffer() {
    auto buf = sender_.make_message_buffer();                           // frame 헤더 자리는 infra가 예약
    bool const reserved = buf->reserve_front(msg::command_header_size); // command 헤더 자리 적층
    assert(reserved);
    (void)reserved;
    return buf;
}

bool CommandSender::try_send(
    domain::DeviceId device, device::port::CommandId command_id, std::uint8_t command_type,
    device::port::CommandBuffer message
) {
    if (!message) {
        return false; // 방어
    }
    // 역색인은 confirming도 담으므로 active만 송신 대상으로 거른다(등록 미확인 연결에는 명령 금지).
    Agent const* const target = agents_.find(device);
    if (target == nullptr || target->state() != Agent::State::active) {
        LOG_WARN("command.send.offline", logger::kv("device", device.to_string()));
        return false;
    }

    msg::Command const cmd{.command_id = command_id.value(), .type = command_type};
    if (!msg::encode_front(cmd, *message)) {
        // headroom 계약 위반. make_command_buffer를 거치지 않은 buffer가 들어온 버그 신호
        LOG_ERROR("command.send.encode_fail", logger::kv("command", command_id.value()));
        return false;
    }
    sender_.send(target->conn(), static_cast<std::uint8_t>(msg::type_of<msg::Command>), std::move(message));
    return true;
}

} // namespace ddcs::ctrl::app::agent
