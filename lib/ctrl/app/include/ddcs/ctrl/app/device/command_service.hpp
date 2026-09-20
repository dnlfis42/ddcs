#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/port/command_failure_sink.hpp"
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

// Device별로 명령 종류마다 처리 중인 명령을 하나씩 관리한다.
class CommandService {
public:
    // 명령의 최종 결과와 개별 전송 시도의 실패 횟수를 구분해 누적한다.
    // dispatched는 첫 전송에 성공한 명령만 센다. 같은 시점의 집계는 다음 관계를 만족한다.
    // dispatched = succeeded + failed + superseded + pending 이다.
    struct Metrics {
        std::uint64_t dispatched_total{};
        std::uint64_t superseded_total{}; // 새 명령으로 교체된 기존 명령 수
        std::uint64_t succeeded_total{};
        std::uint64_t failed_exhausted_total{};               // 최대 시도 횟수 도달
        std::uint64_t failed_offline_total{};                 // 재송신 중 연결 없음
        std::uint64_t failed_encode_fail_total{};             // 재전송 메시지 인코딩 실패
        std::uint64_t dispatch_failures_offline_total{};      // 첫 송신이 연결 없음으로 실패
        std::uint64_t dispatch_failures_encode_fail_total{};  // 첫 전송 메시지 인코딩 실패
        std::uint64_t attempt_failures_agent_failure_total{}; // Agent가 보고한 명령 실행 실패
        std::uint64_t attempt_failures_timeout_total{};       // 명령 결과의 응답 기한 초과
        std::uint64_t resends_total{};                        // 전송 계층에서 수락한 재전송 횟수
        std::uint64_t stale_responses_total{}; // 이미 종료되거나 교체된 명령에 대한 응답
        std::uint64_t rtt_us_sum{};            // 첫 전송부터 성공 결과 수신까지 걸린 시간의 합(마이크로초)

        // RTT 버킷 경계는 마이크로초 단위이며, Prometheus 출력 시 초로 변환한다.
        static constexpr std::array<std::uint64_t, 10> rtt_bucket_bounds_us{
            1'000, 2'000, 5'000, 10'000, 20'000, 50'000, 100'000, 250'000, 500'000, 1'000'000
        };

        // 성공한 명령의 RTT를 버킷별로 센다. 마지막 버킷은 모든 경계를 초과한 값을 담는다.
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

    // 명령을 전송하고 응답 기한을 등록한다. 같은 Device의 같은 종류 명령은 교체한다.
    // 첫 전송에 실패하면 유효하지 않은 ID를 반환한다. 기존 명령의 교체는 취소하지 않는다.
    // failure_sink는 등록된 명령의 최종 실패를 받는다. 먼저 소멸한다면 detach_failure_sink로 해제해야 한다.
    port::CommandId dispatch(
        domain::DeviceId device, wire::command::Command command, common::Clock::time_point now,
        port::CommandFailureSink* failure_sink = nullptr
    );

    // 처리 중인 명령에서 해당 통지 대상을 제거해 소멸 후 콜백 호출을 막는다.
    void detach_failure_sink(port::CommandFailureSink& sink) noexcept;

    // CommandAck를 받으면 결과 응답 기한을 연장한다. 추적 중이지 않은 ID의 응답은 무시한다.
    void
    acknowledge(domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now);

    // CommandOutcome 반영:
    // 성공 시 RTT를 기록하고 명령 처리를 종료한다.
    // 실패 시 남은 시도 횟수에 따라 재전송하거나 최종 실패로 처리한다.
    // 추적 중이지 않은 ID의 응답은 무시한다.
    // code는 수신한 원본 바이트이며, 알려진 결과 코드인지 여기서 판단한다.
    // 알 수 없는 코드도 확인할 수 있도록 로그에는 바이트 값을 그대로 기록한다.
    void settle(
        domain::DeviceId device, port::CommandId command_id, bool success, std::uint8_t code,
        common::Clock::time_point now
    );

    // 응답 기한 또는 재전송 시각에 도달한 명령을 처리한다.
    // 응답 기한이 지나면 실패로 처리하고, 재전송 대기가 끝나면 같은 ID로 다시 보낸다.
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
        port::CommandId id;                        // 재전송 시에도 유지하는 명령 ID
        wire::command::Command command;            // 재전송할 명령. variant의 타입으로 명령 종류를 구분한다.
        common::Clock::time_point dispatched_at{}; // 첫 전송 시각. RTT 측정의 기준이다.
        common::Clock::time_point next_at{};       // 응답 대기 중에는 응답 기한, 재전송 대기 중에는 다음 전송 시각
        port::CommandFailureSink* failure_sink = nullptr;
        int attempts = 1;
        State state = State::in_flight;
    };

    struct DeviceCommands {
        std::vector<Slot> slots; // 명령 종류별로 최대 하나를 보관하며 선형 탐색한다.
    };

    Slot* find_slot(domain::DeviceId device, port::CommandId command_id);
    void close_slot(domain::DeviceId device, port::CommandId command_id);
    void close_failed_slot(domain::DeviceId device, port::CommandId command_id);
    void fail_attempt(
        domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now,
        AttemptFailureReason reason
    );
    void record_dispatch_failure(port::SendResult result) noexcept;
    void record_final_send_failure(port::SendResult result) noexcept;
    void resend(domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now);
    [[nodiscard]] std::chrono::nanoseconds backoff_for(int attempt) const noexcept;

    port::CommandSender& sender_; // 전송 계층에서 send() 호출마다 메시지를 인코딩한다.
    std::chrono::nanoseconds command_timeout_;
    int max_attempts_;
    std::chrono::nanoseconds backoff_base_;
    std::uint64_t next_command_id_ = 1;
    std::unordered_map<domain::DeviceId, DeviceCommands> pending_;
    Metrics metrics_;
};

} // namespace ddcs::ctrl::app::device
