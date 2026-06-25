#include "ddcs/ctrl/app/device/command_service.hpp"

#include "ddcs/logger/log.hpp"

#include <algorithm>
#include <cassert>
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
    domain::DeviceId device, std::uint8_t command_type, port::CommandBuffer payload,
    common::Clock::time_point now
) {
    if (!payload) {
        return {}; // 방어
    }
    auto& device_commands = pending_[device];
    // supersede: 의도 교체는 송신 성패와 무관하게 dispatch 순간 일어난다.
    auto const old_slot = std::find_if(
        device_commands.slots.begin(), device_commands.slots.end(),
        [command_type](Slot const& slot) { return slot.type == command_type; }
    );
    if (old_slot != device_commands.slots.end()) {
        ++superseded_total_;
        LOG_INFO(
            "command.superseded", logger::kv("command", old_slot->id.get()),
            logger::kv("device", device.to_string())
        );
        device_commands.slots.erase(old_slot);
    }

    port::CommandId const command_id{next_command_id_++};
    port::CommandBuffer retained;
    if (max_attempts_ > 1) {
        retained = sender_.make_command_buffer(); // 보관본은 헤더 미기록 상태로 동결된다.
        bool const copied = retained->try_append(payload->data_span());
        assert(copied); // 동일 용량 + 동일 headroom이라 항상 들어간다.
        (void)copied;
    }
    if (!sender_.try_send(device, command_id, command_type, std::move(payload))) {
        LOG_WARN("command.dispatch.fail", logger::kv("device", device.to_string()));
        if (device_commands.slots.empty()) {
            pending_.erase(device); // 빈 항목 잔류 방지
        }
        return {};
    }
    device_commands.slots.push_back(
        Slot{
            .id = command_id,
            .type = command_type,
            .retained = std::move(retained),
            .dispatched_at = now,
            .next_at = after(now, command_timeout_),
            .attempts = 1,
            .phase = Phase::in_flight,
        }
    );
    ++dispatched_total_;

    LOG_INFO(
        "command.dispatch", logger::kv("device", device.to_string()),
        logger::kv("command", command_id.get())
    );
    return command_id;
}

void CommandService::acknowledge(
    domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        ++stale_total_;
        LOG_DEBUG("command.stale", logger::kv("command", command_id.get()));
        return;
    }

    slot->phase = Phase::in_flight;
    slot->next_at = after(now, command_timeout_); // 작동 확인 후 outcome까지 연장
    LOG_INFO("command.ack", logger::kv("command", command_id.get()));
}

void CommandService::settle(
    domain::DeviceId device, port::CommandId command_id, bool success, std::string_view reason,
    common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        ++stale_total_;
        LOG_DEBUG("command.stale", logger::kv("command", command_id.get()));
        return;
    }

    if (!success) {
        LOG_WARN(
            "command.nack", logger::kv("command", command_id.get()), logger::kv("reason", reason)
        );
        fail_attempt(device, command_id, now); // NACK 시 재시도 또는 포기
        return;
    }

    auto const rtt = now - slot->dispatched_at;
    rtt_ms_sum_ += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(rtt).count()
    );
    ++completed_total_;
    LOG_INFO("command.outcome", logger::kv("command", command_id.get()));
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
            ++timed_out_total_;
            LOG_WARN(
                "command.timeout", logger::kv("command", command_id.get()),
                logger::kv("device", device.to_string())
            );
            fail_attempt(device, command_id, now);
        } else {
            resend(device, command_id, now); // backoff 경과 시 동일 id 재전송
        }
    }
}

void CommandService::fail_attempt(
    domain::DeviceId device, port::CommandId command_id, common::Clock::time_point now
) {
    Slot* const slot = find_slot(device, command_id);
    if (slot == nullptr) {
        return;
    }

    if (slot->attempts >= max_attempts_) {
        ++gave_up_total_;
        LOG_WARN(
            "command.gave_up", logger::kv("command", command_id.get()),
            logger::kv("device", device.to_string()), logger::kv("attempts", slot->attempts)
        );
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

    assert(slot->retained); // backoff 진입은 max_attempts > 1에서만이라 보관본 존재
    auto copy = sender_.make_command_buffer(); // 보관본은 불변. 헤더는 사본이 받는다
    bool const copied = copy->try_append(slot->retained->data_span());
    assert(copied);
    (void)copied;
    if (!sender_.try_send(device, slot->id, slot->type, std::move(copy))) {
        ++gave_up_total_;
        LOG_WARN(
            "command.gave_up", logger::kv("command", command_id.get()),
            logger::kv("device", device.to_string()), logger::kv("reason", "send_fail")
        );
        close_slot(device, command_id);
        return;
    }

    ++retried_total_;
    slot->attempts += 1;
    slot->phase = Phase::in_flight;
    slot->next_at = after(now, command_timeout_);
    LOG_INFO(
        "command.retry", logger::kv("command", command_id.get()),
        logger::kv("device", device.to_string()), logger::kv("attempt", slot->attempts)
    );
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
