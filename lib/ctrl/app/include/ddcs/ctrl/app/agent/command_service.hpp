#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

#include <cstddef>
#include <cstdint>

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;

// 명령 발송/상관(command_id) use-case. controller->agent 로 Command 를 보내고
// CommandAck/CommandOutcome 으로 미결(pending)을 닫는다. 타임아웃은 app 측 Clock + sweep.
//  - dispatch       : command_id 발급 + Command 송신 + pending 등록(deadline = now + timeout)
//  - handle_ack     : 수신 확인 -> deadline 연장(agent 작동 중)
//  - handle_outcome : success -> 종료(RTT 기록), NACK -> 실패 시도(재시도/포기)
//  - sweep          : in_flight deadline 초과 -> 실패 시도. backoff 경과 -> 재발송.
// 부분실패 보상: 무응답(timeout) 또는 NACK 시 지수 backoff 후 **새 command_id** 로 재발송(agent dedup 회피),
// max_attempts 초과 시 포기(WARN + gave_up). payload 는 재발송 위해 Pending 에 보관. 주기 구동은 조립 루트.
class CommandService {
public:
    CommandService(
        SessionRegistry& sessions, Outbound& outbound, common::Clock& clock,
        std::chrono::nanoseconds command_timeout = std::chrono::seconds{5}, int max_attempts = 1,
        std::chrono::nanoseconds backoff_base = std::chrono::milliseconds{500}
    ) noexcept;

    // agent 의 현재 conn 으로 Command(type/payload 는 opaque) 송신 + pending 등록.
    // 반환: 발급된 command_id. agent 미연결이면 0(무효, 송신 안 함).
    std::uint64_t dispatch(DeviceId agent, std::uint8_t type, std::string payload);

    void handle_ack(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body);     // a->c: deadline 연장
    void handle_outcome(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body); // a->c: 종료/NACK

    // in_flight deadline 초과 -> 실패 시도(재시도 또는 포기). backoff 경과한 pending -> 재발송.
    void sweep();

    std::size_t pending_count() const noexcept { return pending_.size(); }

    // 누적 메트릭(monotonic). 실패율 = gave_up/dispatched, 평균 RTT = rtt_ms_sum/completed.
    std::uint64_t dispatched_total() const noexcept { return dispatched_total_; }
    std::uint64_t completed_total() const noexcept { return completed_total_; }
    std::uint64_t timed_out_total() const noexcept { return timed_out_total_; } // 응답 timeout(시도 단위)
    std::uint64_t retried_total() const noexcept { return retried_total_; }
    std::uint64_t gave_up_total() const noexcept { return gave_up_total_; } // 재시도 소진(최종 실패, 알람)
    std::uint64_t rtt_ms_sum() const noexcept { return rtt_ms_sum_; }

private:
    enum class Phase : std::uint8_t { in_flight, backoff };

    struct Pending {
        ConnectionId conn; // 현재 attempt 의 target conn (ack/outcome conn 검증용)
        DeviceId agent;
        std::uint8_t type{};
        std::string payload;                       // 재발송용
        common::Clock::time_point dispatched_at{}; // 최초 dispatch(총 RTT 기준)
        common::Clock::time_point next_at{};       // in_flight=응답 deadline / backoff=재시도 시각
        int attempts{1};
        bool acked{false};
        Phase phase{Phase::in_flight};
    };

    bool send_command(ConnectionId conn, std::uint64_t command_id, std::uint8_t type, std::string const& payload);
    Pending* correlate(ConnectionId conn, std::uint64_t command_id);
    void fail_attempt(std::uint64_t command_id); // timeout/NACK -> backoff 또는 give-up
    void redispatch(std::uint64_t command_id);   // backoff 경과 -> 새 id 재발송
    std::chrono::nanoseconds backoff_for(int attempt) const noexcept;

    SessionRegistry& sessions_;
    Outbound& outbound_;
    common::Clock& clock_;
    std::chrono::nanoseconds command_timeout_;
    int max_attempts_;
    std::chrono::nanoseconds backoff_base_;
    std::uint64_t next_command_id_{1}; // 1 부터(0 = 무효)
    std::unordered_map<std::uint64_t, Pending> pending_;

    std::uint64_t dispatched_total_{};
    std::uint64_t completed_total_{};
    std::uint64_t timed_out_total_{};
    std::uint64_t retried_total_{};
    std::uint64_t gave_up_total_{};
    std::uint64_t rtt_ms_sum_{}; // dispatch->outcome 지연(ms) 합(성공 한정)
};

} // namespace ddcs::ctrl::app::agent
