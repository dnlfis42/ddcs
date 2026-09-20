#include "ddcs/ctrl/app/device/command_service.hpp"

#include "ddcs/logger/event.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace ddcs::ctrl::app::device {

namespace {

common::Clock::time_point after(common::Clock::time_point base, std::chrono::nanoseconds d) {
    return base + std::chrono::duration_cast<common::Clock::duration>(d);
}

} // namespace

CommandService::CommandService(
    port::CommandSender& sender, std::chrono::nanoseconds command_timeout, int max_attempts,
    std::chrono::nanoseconds backoff_base
) noexcept
    : sender_{sender},
      command_timeout_{command_timeout},
      max_attempts_{max_attempts},
      backoff_base_{backoff_base} {}

std::size_t CommandService::pending_count() const noexcept {
    std::size_t total = 0;
    for (auto const& [device, commands] : pending_) {
        total += commands.slots.size();
    }
    return total;
}

port::CommandId CommandService::dispatch(
    domain::DeviceId device, wire::command::Command command, common::Clock::time_point now,
    port::CommandFailureSink* failure_sink
) {
    auto& device_commands = pending_[device];
    // 새 명령의 전송 결과와 관계없이 기존의 같은 종류 명령을 교체한다.
    auto const family = command.index();
    auto const old_slot = std::find_if(
        device_commands.slots.begin(), device_commands.slots.end(),
        [family](Slot const& slot) { return slot.command.index() == family; }
    );
    if (old_slot != device_commands.slots.end()) {
        ++metrics_.superseded_total;
        LOG_COMMAND_SUPERSEDE(device.to_string(), old_slot->id.get());
        device_commands.slots.erase(old_slot);
    }

    port::CommandId const command_id{next_command_id_++};
    if (auto const sent = sender_.send(device, command_id, command); sent != port::SendResult::ok) {
        record_dispatch_failure(sent);
        LOG_COMMAND_DISPATCH_FAIL(device.to_string(), command_id.get(), port::to_string(sent));
        if (device_commands.slots.empty()) {
            pending_.erase(device); // 빈 항목 잔류 방지
        }
        return {};
    }
    device_commands.slots.push_back(
        Slot{
            .id = command_id,
            .command = std::move(command),
            .dispatched_at = now,
            .next_at = after(now, command_timeout_),
            .failure_sink = failure_sink,
            .attempts = 1,
            .state = State::in_flight,
        }
    );
    ++metrics_.dispatched_total;

    LOG_COMMAND_DISPATCH(device.to_string(), command_id.get());
    return command_id;
}

void CommandService::acknowledge(
    domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        ++metrics_.stale_responses_total;
        LOG_COMMAND_STALE_RESPONSE(device.to_string(), command_id.get());
        return;
    }

    slot->state = State::in_flight;
    slot->next_at = after(now, command_timeout_); // 수신 확인 시 결과 응답 기한을 연장한다.
    LOG_COMMAND_ACK(device.to_string(), command_id.get(), slot->attempts);
}

void CommandService::settle(
    domain::DeviceId device, port::CommandId command_id, bool success, std::uint8_t code,
    common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        ++metrics_.stale_responses_total;
        LOG_COMMAND_STALE_RESPONSE(device.to_string(), command_id.get());
        return;
    }

    if (!success) {
        LOG_COMMAND_REJECT(device.to_string(), command_id.get(), static_cast<std::uint64_t>(code));
        fail_attempt(device, command_id, now, AttemptFailureReason::agent_failure);
        return;
    }

    auto const rtt = now - slot->dispatched_at;
    auto const rtt_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(rtt).count()
    );
    metrics_.rtt_us_sum += rtt_us;
    ++metrics_.succeeded_total;
    // RTT 이상인 첫 경계의 버킷에 기록한다. 모든 경계를 초과하면 마지막 버킷에 기록한다.
    std::size_t bucket = 0;
    while (bucket < Metrics::rtt_bucket_bounds_us.size() &&
           rtt_us > Metrics::rtt_bucket_bounds_us[bucket]) {
        ++bucket;
    }
    ++metrics_.rtt_buckets[bucket];
    // 로그는 기존 rtt_ms 형식을 유지한다. 메트릭은 마이크로초로 누적해 초 단위로 출력한다.
    LOG_COMMAND_COMPLETE(device.to_string(), command_id.get(), rtt_us / 1'000);
    close_slot(device, command_id); // 성공한 명령의 추적을 종료한다.
}

void CommandService::sweep(common::Clock::time_point now) {
    std::vector<std::pair<domain::DeviceId, port::CommandId>>
        due; // 순회 중 목록을 변경하지 않도록 처리할 명령을 먼저 모은다.
    for (auto const& [device, commands] : pending_) {
        for (auto const& slot : commands.slots) {
            if (slot.next_at < now) {
                due.emplace_back(device, slot.id);
            }
        }
    }
    for (auto const& [device, command_id] : due) {
        Slot const* const slot = find_slot(device, command_id);
        if (slot == nullptr) {
            continue; // 이미 제거된 명령은 건너뛴다.
        }

        if (slot->state == State::in_flight) {
            LOG_COMMAND_TIMEOUT(device.to_string(), command_id.get(), slot->attempts);
            fail_attempt(device, command_id, now, AttemptFailureReason::timeout);
        } else {
            resend(device, command_id, now); // 대기 시간이 지나면 같은 ID로 재전송한다.
        }
    }
}

void CommandService::fail_attempt(
    domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now,
    AttemptFailureReason reason
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        return;
    }

    switch (reason) {
    case AttemptFailureReason::agent_failure:
        ++metrics_.attempt_failures_agent_failure_total;
        break;
    case AttemptFailureReason::timeout:
        ++metrics_.attempt_failures_timeout_total;
        break;
    }

    if (slot->attempts >= max_attempts_) {
        ++metrics_.failed_exhausted_total;
        LOG_COMMAND_FAIL(device.to_string(), command_id.get(), slot->attempts, "exhausted");
        close_failed_slot(device, command_id);
        return;
    }

    slot->state = State::backoff; // 시도 횟수에 따라 늘어나는 대기 시간을 적용한다.
    slot->next_at = after(now, backoff_for(slot->attempts));
}

void CommandService::resend(
    domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        return;
    }

    if (auto const sent = sender_.send(device, slot->id, slot->command);
        sent != port::SendResult::ok) {
        record_final_send_failure(sent);
        LOG_COMMAND_FAIL(
            device.to_string(), command_id.get(), slot->attempts, port::to_string(sent)
        );
        close_failed_slot(device, command_id);
        return;
    }

    ++metrics_.resends_total;
    slot->attempts += 1;
    slot->state = State::in_flight;
    slot->next_at = after(now, command_timeout_);
    LOG_COMMAND_RETRY(device.to_string(), command_id.get(), slot->attempts);
}

void CommandService::record_dispatch_failure(port::SendResult result) noexcept {
    switch (result) {
    case port::SendResult::ok:
        break;
    case port::SendResult::offline:
        ++metrics_.dispatch_failures_offline_total;
        break;
    case port::SendResult::encode_fail:
        ++metrics_.dispatch_failures_encode_fail_total;
        break;
    }
}

void CommandService::record_final_send_failure(port::SendResult result) noexcept {
    switch (result) {
    case port::SendResult::ok:
        break;
    case port::SendResult::offline:
        ++metrics_.failed_offline_total;
        break;
    case port::SendResult::encode_fail:
        ++metrics_.failed_encode_fail_total;
        break;
    }
}

std::chrono::nanoseconds CommandService::backoff_for(int attempt) const noexcept {
    auto d = backoff_base_;
    for (int i = 1; i < attempt && i < 16; ++i) { // 기준 대기 시간을 최대 15번 두 배로 늘린다.
        d *= 2;
    }
    return d;
}

CommandService::Slot*
CommandService::find_slot(domain::DeviceId device, port::CommandId command_id) {
    auto const it = pending_.find(device);
    if (it == pending_.end()) {
        return nullptr;
    }
    for (auto& slot : it->second.slots) {
        if (slot.id == command_id) {
            return &slot;
        }
    }
    return nullptr;
}

void CommandService::detach_failure_sink(port::CommandFailureSink& sink) noexcept {
    for (auto& [device, commands] : pending_) {
        for (auto& slot : commands.slots) {
            if (slot.failure_sink == &sink) {
                slot.failure_sink = nullptr;
            }
        }
    }
}

void CommandService::close_failed_slot(domain::DeviceId device, port::CommandId command_id) {
    auto const* slot = find_slot(device, command_id);
    auto* sink = slot ? slot->failure_sink : nullptr;
    close_slot(device, command_id); // 콜백이 명령 목록을 변경할 수 있으므로 먼저 제거하고 이후에는 슬롯을 참조하지 않는다.
    if (sink) {
        sink->on_command_failed(device, command_id);
    }
}

void CommandService::close_slot(domain::DeviceId device, port::CommandId command_id) {
    auto const it = pending_.find(device);
    if (it == pending_.end()) {
        return;
    }

    std::erase_if(it->second.slots, [command_id](Slot const& slot) {
        return slot.id == command_id;
    });

    if (it->second.slots.empty()) {
        pending_.erase(it);
    }
}

} // namespace ddcs::ctrl::app::device
