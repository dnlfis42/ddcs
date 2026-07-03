#include "ddcs/ctrl/app/session/command_sender.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/message/command.hpp"
#include "ddcs/wire/message/message.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

namespace ddcs::ctrl::app::session {

namespace msg = ddcs::wire::message;

CommandSender::CommandSender(SessionRegistry& sessions, port::MessageSender& sender) noexcept
    : sessions_(sessions),
      sender_(sender) {}

bool CommandSender::try_send(
    domain::DeviceId device, device::port::CommandId command_id,
    device::port::Command const& command
) {
    // 역색인은 confirming도 담으므로 active만 송신 대상으로 거른다(등록 미확인 연결에는 명령 금지).
    Session const* const target = sessions_.find(device);
    if (target == nullptr || target->state() != Session::State::active) {
        LOG_WARN("command.send.offline", logger::kv("device", device.to_string()));
        return false;
    }

    auto message = sender_.make_message_buffer(); // frame 헤더 자리는 infra가 예약
    bool const reserved =
        message->try_grow_headroom(msg::command_request_header_size); // command 헤더 자리 적층
    assert(reserved);
    (void)reserved;

    // typed 명령을 wire payload로 인코딩한다. Mode<->wire 매핑은 device 커널을 경유한다.
    std::uint8_t command_type = 0;
    std::optional<std::size_t> written;
    std::visit(
        [&](device::port::SetMode const& set_mode) {
            command_type = static_cast<std::uint8_t>(msg::CommandType::set_mode);
            written = msg::encode_set_mode(
                message->tailroom_span(), ddcs::device::encode_mode(set_mode.mode)
            );
        },
        command
    );
    if (!written || !message->try_commit(*written)) {
        LOG_ERROR("command.send.encode_fail", logger::kv("command", command_id.get()));
        return false;
    }

    // command 헤더(`[type][command_id][command_type]`)를 headroom에 제자리 prepend한다.
    // (복사 없는 조립 경로)
    std::array<std::byte, msg::command_request_header_size> header{};
    auto const written_header =
        msg::encode_command_request_header(header, command_id.get(), command_type);
    if (!written_header || !message->try_prepend(header)) {
        LOG_ERROR("command.send.encode_fail", logger::kv("command", command_id.get()));
        return false;
    }
    sender_.send(target->conn(), std::move(message));
    return true;
}

} // namespace ddcs::ctrl::app::session
