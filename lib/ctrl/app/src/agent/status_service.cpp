#include "ddcs/ctrl/app/agent/status_service.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/proto/msg/message.hpp"

#include <string_view>

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::app::session::State;

namespace {

double double_of(json::Value const& obj, std::string_view key) noexcept {
    auto const* v = obj.find(key);
    return (v != nullptr) ? v->as_double().value_or(0.0) : 0.0;
}

device::Mode mode_of(json::Value const& obj) noexcept {
    auto const* m = obj.find("mode");
    auto const sv = (m != nullptr) ? m->as_string().value_or(std::string_view{}) : std::string_view{};
    return device::from_string(sv).value_or(device::Mode::safe); // 미지 -> safe
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

    auto const parsed = json::Value::parse(st.status_json);
    if (!parsed || !parsed->is_object()) {
        LOG_WARN("agent.status.bad_json", ddcs::logger::kv("conn", conn.value()));
        return;
    }

    registry_.update_telemetry(
        session->agent, mode_of(*parsed), double_of(*parsed, "load"), double_of(*parsed, "temp")
    );
    LOG_DEBUG(
        "agent.status", ddcs::logger::kv("agent", session->agent.value()), ddcs::logger::kv("status", st.status_json)
    );
}

} // namespace ddcs::ctrl::app::agent
