#include "ddcs/ctrl/app/session/command_sender.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/wire/command/command.hpp"
#include "ddcs/wire/message/message.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace ddcs::ctrl::app::session {

namespace msg = ddcs::wire::message;
namespace cmd = ddcs::wire::command;

CommandSender::CommandSender(SessionRegistry& sessions, port::MessageSender& sender) noexcept
    : sessions_(sessions),
      sender_(sender) {}

device::port::SendResult CommandSender::send(
    domain::DeviceId device, device::port::CommandId command_id,
    wire::command::Command const& command
) {
    // 역색인은 confirming도 담으므로 active만 송신 대상으로 거른다(등록 미확인 연결에는 명령 금지).
    Session const* const target = sessions_.find(device);
    if (target == nullptr || !target->active()) {
        return device::port::SendResult::offline;
    }

    auto message = sender_.make_message_buffer(); // frame 헤더 자리는 infra가 예약
    (void)message->grow_headroom(msg::command_request_header_size); // command 헤더 자리 적층

    // 명령 payload를 인코딩한다. Mode<->wire byte 매핑은 발행 지점이 커널(encode_mode)로 마쳤다.
    std::uint8_t command_type = 0;
    std::optional<std::size_t> written;
    std::visit(
        [&](cmd::SetMode const& set_mode) {
            command_type = static_cast<std::uint8_t>(cmd::CommandType::set_mode);
            written = cmd::encode_set_mode(message->tailroom_span(), set_mode.mode);
        },
        command
    );
    if (!written || !message->commit(*written)) {
        LOG_MESSAGE_ENCODE_FAIL(msg::to_string(msg::MessageType::command_request));
        return device::port::SendResult::encode_fail;
    }

    // command 헤더(`[type][command_id][command_type]`)를 headroom에 제자리 prepend한다.
    // (복사 없는 조립 경로)
    std::array<std::byte, msg::command_request_header_size> header{};
    auto const written_header =
        msg::encode_command_request_header(header, command_id.get(), command_type);
    if (!written_header || !message->prepend(header)) {
        LOG_MESSAGE_ENCODE_FAIL(msg::to_string(msg::MessageType::command_request));
        return device::port::SendResult::encode_fail;
    }
    sender_.send(target->conn(), std::move(message));
    return device::port::SendResult::ok;
}

} // namespace ddcs::ctrl::app::session
