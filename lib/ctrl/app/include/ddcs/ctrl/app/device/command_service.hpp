#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/wire/command/command.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ddcs::ctrl::app::device {

// 명령 전달 use-case. device 1급 장부에 계열(type)당 미결 슬롯 1개를 유지한다.
class CommandService {
public:
    // 누적 메트릭(monotonic). 논리 명령의 terminal 상태와 개별 시도 실패를 분리한다.
    // dispatched는 첫 송신에 성공해 상태 기계에 들어간 논리 명령만 센다. 따라서 같은 시점에
    // dispatched = succeeded + failed + superseded + pending 이다.
    struct Metrics {
        std::uint64_t dispatched_total{};
        std::uint64_t superseded_total{}; // 새 의도가 옛 미결을 교체(정상 종결)
        std::uint64_t succeeded_total{};
        std::uint64_t failed_exhausted_total{};          // 재시도 budget 소진
        std::uint64_t failed_offline_total{};            // 재송신 중 연결 없음
        std::uint64_t failed_encode_fail_total{};        // 재송신 encode 방어 실패
        std::uint64_t dispatch_failures_offline_total{}; // 첫 송신이 연결 없음으로 실패
        std::uint64_t dispatch_failures_encode_fail_total{};  // 첫 송신 encode 방어 실패
        std::uint64_t attempt_failures_agent_failure_total{}; // Agent outcome 실패
        std::uint64_t attempt_failures_timeout_total{};       // outcome timeout
        std::uint64_t resends_total{};         // 전송 adapter가 수락한 재송신
        std::uint64_t stale_responses_total{}; // 닫힌/대체된 명령의 늦은 응답(정상 부산물)
        std::uint64_t rtt_us_sum{}; // dispatch부터 success outcome까지 지연(us) 합

        // RTT 히스토그램 버킷 경계(us). Prometheus에는 seconds로 정확히 변환해 노출한다.
        static constexpr std::array<std::uint64_t, 10> rtt_bucket_bounds_us{
            1'000, 2'000, 5'000, 10'000, 20'000, 50'000, 100'000, 250'000, 500'000, 1'000'000
        };

        // 버킷별 관측 수(비누적, 성공 한정). 마지막(index 10)은 마지막 경계 초과(+Inf) 오버플로.
        std::array<std::uint64_t, 11> rtt_buckets{};
    };

    explicit CommandService(
        port::CommandSender& sender,
        std::chrono::nanoseconds command_timeout = std::chrono::seconds{5}, int max_attempts = 1,
        std::chrono::nanoseconds backoff_base = std::chrono::milliseconds{500}
    ) noexcept;

    [[nodiscard]] std::size_t pending_count() const noexcept;

    [[nodiscard]] Metrics const& metrics() const noexcept {
        return metrics_;
    }

    // 논리 명령 발송 + 슬롯 등록(deadline은 now + timeout). 같은 (device, 계열) 미결은 supersede.
    // 송신 실패면 invalid를 반환한다(이때도 supersede는 유효하다).
    port::CommandId dispatch(
        domain::DeviceId device, wire::command::Command command, common::Clock::time_point now
    );

    // CommandAck 반영: 수신 확인 시 deadline 연장. 미지 id는 stale
    void
    acknowledge(domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now);

    // CommandOutcome 반영:
    // - 성공은 슬롯 종료(RTT 기록)
    // - Agent failure는 시도 실패(재시도/소진)
    // - 미지 id는 stale
    // code는 검증 전 wire byte다. codec이 어휘 밖 값을 통과시키므로 해석은 여기 몫이고,
    // 로그에는 어휘 이름이 아니라 byte 그대로 싣는다(어휘 밖이면 이름이 빈 문자열이 된다).
    void settle(
        domain::DeviceId device, port::CommandId command_id, bool success, std::uint8_t code,
        common::Clock::time_point now
    );

    // 만기 슬롯 처리:
    // - in_flight 초과 시 시도 실패, backoff 경과 시 동일 id 재전송
    void sweep(common::Clock::time_point now);

private:
    enum class State : std::uint8_t {
        in_flight,
        backoff,
    };

    enum class AttemptFailureReason : std::uint8_t {
        agent_failure,
        timeout,
    };

    struct Slot {
        port::CommandId id;             // 논리 명령의 이름. 재전송에도 불변
        wire::command::Command command; // 재전송 보관본. 계열 = variant 대안
        common::Clock::time_point dispatched_at{}; // 최초 dispatch(총 RTT 기준)
        common::Clock::time_point next_at{}; // in_flight=응답 deadline / backoff=재전송 시각
        int attempts = 1;
        State state = State::in_flight;
    };

    struct DeviceCommands {
        std::vector<Slot> slots; // 계열당 최대 1개. 작은 집합이라 선형 탐색
    };

    Slot* find_slot(domain::DeviceId device, port::CommandId command_id);
    void close_slot(domain::DeviceId device, port::CommandId command_id);
    void fail_attempt(
        domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now,
        AttemptFailureReason reason
    );
    void record_dispatch_failure(port::SendResult result) noexcept;
    void record_final_send_failure(port::SendResult result) noexcept;
    void resend(domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now);
    [[nodiscard]] std::chrono::nanoseconds backoff_for(int attempt) const noexcept;

    port::CommandSender& sender_; // wire 인코딩은 어댑터가 send마다 수행
    std::chrono::nanoseconds command_timeout_;
    int max_attempts_;
    std::chrono::nanoseconds backoff_base_;
    std::uint64_t next_command_id_ = 1;
    std::unordered_map<domain::DeviceId, DeviceCommands> pending_;
    Metrics metrics_;
};

} // namespace ddcs::ctrl::app::device
