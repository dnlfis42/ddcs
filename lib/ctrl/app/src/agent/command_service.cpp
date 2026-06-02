#include "ddcs/ctrl/app/agent/command_service.hpp"

#include "ddcs/logger/log.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <cstdint>

namespace ddcs::ctrl::app::agent {

namespace {
common::Clock::time_point after(common::Clock::time_point base, std::chrono::nanoseconds d) {
    return base + std::chrono::duration_cast<common::Clock::duration>(d);
}
} // namespace

CommandService::CommandService(
    SessionRegistry& sessions, Outbound& outbound, common::Clock& clock, std::chrono::nanoseconds command_timeout,
    int max_attempts, std::chrono::nanoseconds backoff_base
) noexcept
    : sessions_{sessions}, outbound_{outbound}, clock_{clock}, command_timeout_{command_timeout},
      max_attempts_{max_attempts}, backoff_base_{backoff_base} {}

bool CommandService::send_command(
    ConnectionId conn, std::uint64_t command_id, std::uint8_t type, std::string const& payload
) {
    proto::msg::Command const cmd{.command_id = command_id, .type = type, .payload = payload};
    auto buf = outbound_.payload_buffer();
    if (!proto::msg::encode(cmd, *buf)) {
        LOG_WARN("command.encode_fail", ddcs::logger::kv("command", command_id));
        return false;
    }
    outbound_.send(conn, static_cast<std::uint8_t>(proto::msg::type_of<proto::msg::Command>), std::move(buf));
    return true;
}

std::uint64_t CommandService::dispatch(AgentId agent, std::uint8_t type, std::string payload) {
    ConnectionId const conn = sessions_.resolve(agent);
    if (!conn.valid()) {
        LOG_WARN("command.dispatch.offline", ddcs::logger::kv("agent", agent.value()));
        return 0; // agent 미연결 -> 무효
    }

    std::uint64_t const command_id = next_command_id_++;
    if (!send_command(conn, command_id, type, payload)) {
        return 0; // 방어 (command_id gap 은 무해)
    }

    auto const now = clock_.now();
    pending_.emplace(
        command_id, Pending{
                        .conn = conn,
                        .agent = agent,
                        .type = type,
                        .payload = std::move(payload),
                        .dispatched_at = now,
                        .next_at = after(now, command_timeout_),
                        .attempts = 1,
                        .acked = false,
                        .phase = Phase::in_flight,
                    }
    );
    ++dispatched_total_;
    LOG_INFO(
        "command.dispatch", ddcs::logger::kv("agent", agent.value()), ddcs::logger::kv("conn", conn.value()),
        ddcs::logger::kv("command", command_id)
    );
    return command_id;
}

void CommandService::handle_ack(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body) {
    proto::msg::CommandAck ack{};
    if (!proto::msg::decode(body->readable(), ack)) {
        LOG_WARN("command.ack.decode_fail", ddcs::logger::kv("conn", conn.value()));
        return;
    }
    Pending* p = correlate(conn, ack.command_id);
    if (p == nullptr) {
        return;
    }
    p->acked = true;
    p->phase = Phase::in_flight;
    p->next_at = after(clock_.now(), command_timeout_); // 작동 확인 -> outcome 까지 연장
    LOG_INFO("command.ack", ddcs::logger::kv("command", ack.command_id));
}

void CommandService::handle_outcome(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body) {
    proto::msg::CommandOutcome outcome{};
    if (!proto::msg::decode(body->readable(), outcome)) {
        LOG_WARN("command.outcome.decode_fail", ddcs::logger::kv("conn", conn.value()));
        return;
    }
    Pending const* const p = correlate(conn, outcome.command_id);
    if (p == nullptr) {
        return;
    }
    if (outcome.result != proto::msg::CommandResult::success) {
        LOG_WARN("command.nack", ddcs::logger::kv("command", outcome.command_id));
        fail_attempt(outcome.command_id); // NACK -> 재시도 또는 포기
        return;
    }
    auto const rtt = clock_.now() - p->dispatched_at;
    rtt_ms_sum_ += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(rtt).count());
    ++completed_total_;
    LOG_INFO("command.outcome", ddcs::logger::kv("command", outcome.command_id));
    pending_.erase(outcome.command_id); // 성공 확정 -> 미결 종료
}

void CommandService::sweep() {
    auto const now = clock_.now();
    std::vector<std::uint64_t> due; // 순회 중 변경 금지 -> 만기 id 먼저 수집
    for (auto const& [id, p] : pending_) {
        if (p.next_at < now) {
            due.push_back(id);
        }
    }
    for (auto const id : due) {
        auto const it = pending_.find(id);
        if (it == pending_.end()) {
            continue; // 방어 (이미 처리)
        }
        if (it->second.phase == Phase::in_flight) {
            ++timed_out_total_;
            LOG_WARN(
                "command.timeout", ddcs::logger::kv("command", id), ddcs::logger::kv("agent", it->second.agent.value())
            );
            fail_attempt(id);
        } else {
            redispatch(id); // backoff 경과 -> 재발송
        }
    }
}

void CommandService::fail_attempt(std::uint64_t command_id) {
    auto const it = pending_.find(command_id);
    if (it == pending_.end()) {
        return;
    }
    Pending& p = it->second;
    if (p.attempts >= max_attempts_) {
        ++gave_up_total_;
        LOG_WARN(
            "command.gave_up", ddcs::logger::kv("command", command_id), ddcs::logger::kv("agent", p.agent.value()),
            ddcs::logger::kv("attempts", p.attempts)
        );
        pending_.erase(it);
        return;
    }
    p.phase = Phase::backoff; // 지수 backoff 후 재발송 대기
    p.next_at = after(clock_.now(), backoff_for(p.attempts));
}

void CommandService::redispatch(std::uint64_t command_id) {
    auto const it = pending_.find(command_id);
    if (it == pending_.end()) {
        return;
    }
    Pending p = std::move(it->second); // payload 보존 (새 id 로 재등록)
    pending_.erase(it);

    ConnectionId const conn = sessions_.resolve(p.agent); // 재연결되었을 수 있음
    if (!conn.valid()) {
        ++gave_up_total_;
        LOG_WARN("command.gave_up", ddcs::logger::kv("agent", p.agent.value()), ddcs::logger::kv("reason", "offline"));
        return;
    }
    std::uint64_t const new_id = next_command_id_++;
    if (!send_command(conn, new_id, p.type, p.payload)) {
        ++gave_up_total_;
        return;
    }
    ++retried_total_;
    p.conn = conn;
    p.attempts += 1;
    p.acked = false;
    p.phase = Phase::in_flight;
    p.next_at = after(clock_.now(), command_timeout_);
    LOG_INFO(
        "command.retry", ddcs::logger::kv("command", new_id), ddcs::logger::kv("agent", p.agent.value()),
        ddcs::logger::kv("attempt", p.attempts)
    );
    pending_.emplace(new_id, std::move(p));
}

std::chrono::nanoseconds CommandService::backoff_for(int attempt) const noexcept {
    auto d = backoff_base_;
    for (int i = 1; i < attempt && i < 16; ++i) { // base * 2^(attempt-1), 16회 cap
        d *= 2;
    }
    return d;
}

CommandService::Pending* CommandService::correlate(ConnectionId conn, std::uint64_t command_id) {
    auto it = pending_.find(command_id);
    if (it == pending_.end()) {
        LOG_WARN("command.correlate.unknown", ddcs::logger::kv("command", command_id));
        return nullptr; // 모르는/이미 닫힌 command_id (stale)
    }
    if (it->second.conn != conn) {
        LOG_WARN(
            "command.correlate.conn_mismatch", ddcs::logger::kv("command", command_id),
            ddcs::logger::kv("expect", it->second.conn.value()), ddcs::logger::kv("actual", conn.value())
        );
        return nullptr; // 다른 conn 이 남의 명령에 응답 -> 무시
    }
    return &it->second;
}

} // namespace ddcs::ctrl::app::agent
