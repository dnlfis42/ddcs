#pragma once

#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/wire/command/command.hpp"

#include <cstdint>
#include <string_view>

namespace ddcs::ctrl::app::device::port {

// 송신이 왜 안 됐는지. 명령이 어떻게 됐는지는 호출부가 정하므로 여기서는 이유만 돌려준다.
enum class SendResult : std::uint8_t {
    ok,
    offline,     // 미등록이거나 active가 아닌 device
    encode_fail, // 버퍼에 담지 못함(방어 경로)
};

// 로그/진단용 이름. 어휘 밖 값은 빈 문자열로 노출한다.
constexpr std::string_view to_string(SendResult result) noexcept {
    switch (result) {
    case SendResult::ok:
        return "ok";
    case SendResult::offline:
        return "offline";
    case SendResult::encode_fail:
        return "encode_fail";
    }
    return {};
}

// controller에서 agent로 가는 Command 송신 포트.
// device에서 현재 연결로 해소하는 일과 wire 인코딩은 구현 책임이다.
class CommandSender {
public:
    virtual ~CommandSender() = default;

    // best-effort. 실패해도 로그는 남기지 않는다. 명령이 어떻게 됐는지를 아는 호출부가
    // 반환된 이유를 실어 한 줄로 알린다.
    [[nodiscard]] virtual SendResult
    send(domain::DeviceId device, CommandId command_id, wire::command::Command const& command) = 0;
};

} // namespace ddcs::ctrl::app::device::port
