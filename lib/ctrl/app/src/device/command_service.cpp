#include "ddcs/ctrl/app/device/command_service.hpp"

#include "ddcs/logger/event.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string_view>
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
    domain::DeviceId device, wire::command::Command command, common::Clock::time_point now
) {
    auto& device_commands = pending_[device];
    // supersede: 의도 교체는 송신 성패와 무관하게 dispatch 순간 일어난다.
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
        LOG_COMMAND_DISPATCH_FAIL(device.to_string(), command_id.get(), port::to_string(sent));
        if (device_commands.slots.empty()) {
            pending_.erase(device); // 빈 항목 잔류 방지
        }
        return {};
    }
    device_commands.slots.push_back(Slot{
        .id = command_id,
        .command = std::move(command),
        .dispatched_at = now,
        .next_at = after(now, command_timeout_),
        .attempts = 1,
        .phase = Phase::in_flight,
    });
    ++metrics_.dispatched_total;

    LOG_COMMAND_DISPATCH(device.to_string(), command_id.get());
    return command_id;
}

void CommandService::acknowledge(
    domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        ++metrics_.stale_total;
        LOG_COMMAND_STALE_RESPONSE(device.to_string(), command_id.get());
        return;
    }

    slot->phase = Phase::in_flight;
    slot->next_at = after(now, command_timeout_); // 작동 확인 후 outcome까지 연장
    LOG_COMMAND_ACK(device.to_string(), command_id.get(), slot->attempts);
}

void CommandService::settle(
    domain::DeviceId device, port::CommandId command_id, bool success, std::uint8_t code,
    common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        ++metrics_.stale_total;
        LOG_COMMAND_STALE_RESPONSE(device.to_string(), command_id.get());
        return;
    }

    if (!success) {
        LOG_COMMAND_REJECT(device.to_string(), command_id.get(), static_cast<std::uint64_t>(code));
        fail_attempt(device, command_id, now, "reject"); // 거부면 재시도 또는 포기
        return;
    }

    auto const rtt = now - slot->dispatched_at;
    auto const rtt_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(rtt).count()
    );
    metrics_.rtt_ms_sum += rtt_ms;
    ++metrics_.completed_total;
    // 히스토그램: rtt_ms 이상인 첫 경계 버킷에 1, 모든 경계 초과면 +Inf 오버플로 슬롯에 1
    std::size_t bucket = 0;
    while (bucket < Metrics::rtt_bucket_bounds_ms.size() &&
           rtt_ms > Metrics::rtt_bucket_bounds_ms[bucket]) {
        ++bucket;
    }
    ++metrics_.rtt_buckets[bucket];
    LOG_COMMAND_COMPLETE(device.to_string(), command_id.get(), rtt_ms);
    close_slot(device, command_id); // 성공 확정 시 미결 종료
}

void CommandService::sweep(common::Clock::time_point now) {
    std::vector<std::pair<domain::DeviceId, port::CommandId>>
        due; // 순회 중 변경 금지라 만기 슬롯 먼저 수집
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
            continue; // 방어. 이미 처리
        }

        if (slot->phase == Phase::in_flight) {
            ++metrics_.timed_out_total;
            LOG_COMMAND_TIMEOUT(device.to_string(), command_id.get(), slot->attempts);
            fail_attempt(device, command_id, now, "timeout");
        } else {
            resend(device, command_id, now); // backoff 경과 시 동일 id 재전송
        }
    }
}

void CommandService::fail_attempt(
    domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now,
    std::string_view reason
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        return;
    }

    if (slot->attempts >= max_attempts_) {
        ++metrics_.gave_up_total;
        LOG_COMMAND_FAIL(device.to_string(), command_id.get(), slot->attempts, reason);
        close_slot(device, command_id);
        return;
    }

    slot->phase = Phase::backoff; // 지수 backoff 후 재전송 대기
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
        ++metrics_.gave_up_total;
        LOG_COMMAND_FAIL(
            device.to_string(), command_id.get(), slot->attempts, port::to_string(sent)
        );
        close_slot(device, command_id);
        return;
    }

    ++metrics_.retried_total;
    slot->attempts += 1;
    slot->phase = Phase::in_flight;
    slot->next_at = after(now, command_timeout_);
    LOG_COMMAND_RETRY(device.to_string(), command_id.get(), slot->attempts);
}

std::chrono::nanoseconds CommandService::backoff_for(int attempt) const noexcept {
    auto d = backoff_base_;
    for (int i = 1; i < attempt && i < 16; ++i) { // base * 2^(attempt-1), 16회 cap
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
