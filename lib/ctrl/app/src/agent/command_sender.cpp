#include "ddcs/ctrl/app/agent/command_sender.hpp"

#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/acmp/message.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ddcs::ctrl::app::agent {

namespace acmp = ddcs::wire::acmp;

CommandSender::CommandSender(AgentRegistry& agents, port::MessageSender& sender) noexcept
    : agents_{agents},
      sender_{sender} {}

device::port::CommandBuffer CommandSender::make_command_buffer() {
    auto buf = sender_.make_message_buffer(); // frame 헤더 자리는 infra가 예약
    bool const reserved =
        buf->reserve_front(acmp::command_request_header_size); // command 헤더 자리 적층
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

    // command 헤더(`[type][command_id][command_type]`)를 headroom에 제자리 prepend한다.
    // (복사 없는 조립 경로)
    std::array<std::byte, acmp::command_request_header_size> header{};
    auto const written =
        acmp::encode_command_request_header(command_id.value(), command_type, header);
    if (!written || !message->write_front(header)) {
        // headroom 계약 위반. make_command_buffer를 거치지 않은 buffer가 들어온 버그 신호
        LOG_ERROR("command.send.encode_fail", logger::kv("command", command_id.value()));
        return false;
    }
    sender_.send(target->conn(), std::move(message));
    return true;
}

} // namespace ddcs::ctrl::app::agent
