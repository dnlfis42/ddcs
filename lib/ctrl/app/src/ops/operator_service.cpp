#include "ddcs/ctrl/app/ops/operator_service.hpp"

#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/proto/cmd/command.hpp"

#include <string>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace ddcs::ctrl::app::ops {

namespace {
constexpr std::size_t pool_chunk{8};
constexpr std::size_t cmd_buf_capacity{64};
} // namespace

OperatorService::OperatorService(AgentRegistry& registry, CommandService& commands) noexcept
    : registry_{registry}, commands_{commands},
      encode_pool_{common::make_pool<common::LinearBuffer>(0, pool_chunk, cmd_buf_capacity)} {}

std::uint64_t OperatorService::set_mode(AgentUuid const& agent_uuid, device::Mode mode) {
    auto const* agent = registry_.find_by_uuid(agent_uuid);
    if (agent == nullptr) {
        LOG_WARN("operator.set_mode.unknown_agent", ddcs::logger::kv("uuid", agent_uuid.to_string()));
        return 0; // 등록된 적 없는 agent
    }

    auto buf = encode_pool_.acquire();
    if (!proto::cmd::encode(proto::cmd::SetMode{.mode = mode}, *buf)) {
        return 0; // 방어
    }
    auto const r = buf->readable();
    std::string payload{reinterpret_cast<char const*>(r.data()), r.size()};

    auto const command_id =
        commands_.dispatch(agent->id, static_cast<std::uint8_t>(proto::cmd::CommandType::SetMode), std::move(payload));
    LOG_INFO(
        "operator.set_mode", ddcs::logger::kv("agent", agent->id.value()),
        ddcs::logger::kv("mode", static_cast<std::uint64_t>(mode)), ddcs::logger::kv("command", command_id)
    );
    return command_id;
}

} // namespace ddcs::ctrl::app::ops
