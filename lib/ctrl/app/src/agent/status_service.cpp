#include "ddcs/ctrl/app/agent/status_service.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/proto/msg/message.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::app::session::State;

namespace {

device::Mode mode_of(std::uint8_t mode) noexcept {
    switch (static_cast<device::Mode>(mode)) {
    case device::Mode::safe:
        return device::Mode::safe;
    case device::Mode::normal:
        return device::Mode::normal;
    case device::Mode::performance:
        return device::Mode::performance;
    }
    return device::Mode::safe;
}

} // namespace

void StatusService::handle_status(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body) {
    proto::msg::Status st{};
    if (!proto::msg::decode(body->readable(), st)) {
        LOG_WARN("agent.status.decode_fail", ddcs::logger::kv("conn", conn.value()));
        return; // 텔레메트리 -> 드롭(비치명적)
    }

    auto const* session = sessions_.find(conn);
    if (session == nullptr || session->state != State::active) {
        return; // 미등록/비활성 conn 의 status -> 무시
    }

    registry_.update_status(session->agent, device::Status{.mode = mode_of(st.mode), .load = st.load, .temp = st.temp});
    LOG_DEBUG(
        "agent.status", ddcs::logger::kv("agent", session->agent.to_string()),
        ddcs::logger::kv("mode", static_cast<std::uint64_t>(st.mode)), ddcs::logger::kv("load", st.load),
        ddcs::logger::kv("temp", st.temp)
    );
}

} // namespace ddcs::ctrl::app::agent
