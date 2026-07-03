#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/port/command.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ddcs::ctrl::app::device {

// 명령 전달 use-case. device 1급 장부에 계열(type)당 미결 슬롯 1개를 유지한다.
// - 논리 명령 1개당 CommandId 1개, 재전송도 동일 id (agent는 depth-1 dedup으로 재적용 없이 캐시된
// ack/outcome 회신, MQTT/CoAP 관례)
// - 같은 (device, 계열) 재발송은 supersede: 옛 의도는 송신 성패와 무관하게 dispatch 순간 폐기
// - 보관본은 typed 명령 값으로 슬롯에 상주, wire 인코딩은 어댑터가 send마다 수행한다
// - 미지 id 응답(stale)은 재전송/supersede의 정상 부산물: 카운터 + DEBUG로만 관측
// - 주기 구동(sweep)은 조립 루트
class CommandService {
public:
    explicit CommandService(
        port::CommandSender& sender,
        std::chrono::nanoseconds command_timeout = std::chrono::seconds{5}, int max_attempts = 1,
        std::chrono::nanoseconds backoff_base = std::chrono::milliseconds{500}
    ) noexcept;

    [[nodiscard]] std::size_t pending_count() const noexcept;

    // 누적 메트릭(monotonic). 실패율 = gave_up/dispatched, 평균 RTT = rtt_ms_sum/completed
    struct Metrics {
        std::uint64_t dispatched_total{};
        std::uint64_t completed_total{};
        std::uint64_t timed_out_total{}; // 응답 timeout(시도 단위)
        std::uint64_t retried_total{};
        std::uint64_t gave_up_total{};    // 재시도 소진(최종 실패, 알람)
        std::uint64_t superseded_total{}; // 새 의도가 옛 미결을 교체(정상 종결)
        std::uint64_t stale_total{};      // 닫힌/대체된 명령의 늦은 응답(정상 부산물)
        std::uint64_t rtt_ms_sum{};       // dispatch에서 outcome까지 지연(ms) 합(성공 한정)

        // RTT 히스토그램 버킷 경계(ms, le). 평균이 숨기는 꼬리(p99 등)를 백분위로 보기 위함.
        static constexpr std::array<std::uint64_t, 10> rtt_bucket_bounds_ms{1,   2,   5,   10,
                                                                            20,  50,  100, 250,
                                                                            500, 1000};

        // 버킷별 관측 수(비누적, 성공 한정). 마지막(index 10)은 마지막 경계 초과(+Inf) 오버플로.
        std::array<std::uint64_t, 11> rtt_buckets{};
    };

    [[nodiscard]] Metrics const& metrics() const noexcept {
        return metrics_;
    }

    // 논리 명령 발송 + 슬롯 등록(deadline은 now + timeout). 같은 (device, 계열) 미결은 supersede
    // - 송신 실패면 invalid를 반환한다(이때도 supersede는 유효하다).
    port::CommandId
    dispatch(domain::DeviceId device, port::Command command, common::Clock::time_point now);

    // CommandAck 반영: 수신 확인 시 deadline 연장. 미지 id는 stale
    void
    acknowledge(domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now);

    // CommandOutcome 반영:
    // - 성공 시 슬롯 종료 (RTT 기록)
    // - 실패 시 시도 실패 (재시도/포기)
    // - 미지 id는 stale
    void settle(
        domain::DeviceId device, port::CommandId command_id, bool success, std::string_view reason,
        common::Clock::time_point now
    );

    // 만기 슬롯 처리:
    // - in_flight 초과 시 시도 실패, backoff 경과 시 동일 id 재전송
    void sweep(common::Clock::time_point now);

private:
    enum class Phase : std::uint8_t { in_flight, backoff };

    struct Slot {
        port::CommandId id;                        // 논리 명령의 이름. 재전송에도 불변
        port::Command command;                     // 재전송 보관본. 계열 = variant 대안
        common::Clock::time_point dispatched_at{}; // 최초 dispatch(총 RTT 기준)
        common::Clock::time_point next_at{};       // in_flight=응답 deadline / backoff=재전송 시각
        int attempts{1};
        Phase phase{Phase::in_flight};
    };

    struct DeviceCommands {
        std::vector<Slot> slots; // 계열당 최대 1개. 작은 집합이라 선형 탐색
    };

private:
    Slot* find_slot(domain::DeviceId device, port::CommandId command_id);
    void close_slot(domain::DeviceId device, port::CommandId command_id);
    void fail_attempt(
        domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now
    );
    void resend(domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now);
    [[nodiscard]] std::chrono::nanoseconds backoff_for(int attempt) const noexcept;

private:
    port::CommandSender& sender_;
    std::chrono::nanoseconds command_timeout_;
    int max_attempts_;
    std::chrono::nanoseconds backoff_base_;
    std::uint64_t next_command_id_{1}; // 전역 단조 토큰. 1부터(0 = CommandId invalid)
    std::unordered_map<domain::DeviceId, DeviceCommands> pending_;
    Metrics metrics_;
};

} // namespace ddcs::ctrl::app::device
