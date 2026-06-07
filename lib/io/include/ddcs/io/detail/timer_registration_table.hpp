#pragma once

#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <vector>

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace ddcs::io::detail {

// TimerId를 TimerHandler 등록으로 해석하고 cancel/expiry 뒤의 stale id를 거른다.
class TimerRegistrationTable {
public:
    [[nodiscard]] TimerId insert(TimerHandler& handler) {
        std::uint32_t index{};
        if (free_.empty()) {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        } else {
            index = free_.back();
            free_.pop_back();
        }

        Slot& slot = slots_[index];

        slot.handler = &handler;
        slot.active = true;
        return pack(slot.generation, index);
    }

    [[nodiscard]] TimerHandler* consume(TimerId id) noexcept {
        Slot* slot = resolve_slot(id);
        if (slot == nullptr) {
            return nullptr;
        }

        TimerHandler* handler = slot->handler;
        release_slot(unpack_index(id), *slot);
        return handler;
    }

    bool erase(TimerId id) noexcept {
        Slot* slot = resolve_slot(id);
        if (slot == nullptr) {
            return false;
        }

        release_slot(unpack_index(id), *slot);
        return true;
    }

    [[nodiscard]] TimerHandler* resolve(TimerId id) const noexcept {
        Slot const* slot = resolve_slot(id);
        return slot == nullptr ? nullptr : slot->handler;
    }

    [[nodiscard]] bool contains(TimerId id) const noexcept { return resolve_slot(id) != nullptr; }

private:
    struct Slot {
        TimerHandler* handler{nullptr};
        std::uint32_t generation{1};
        bool active{false};
    };

private:
    [[nodiscard]] static constexpr TimerId pack(std::uint32_t generation, std::uint32_t index) noexcept {
        return TimerId{(static_cast<std::uint64_t>(generation) << 32) | index};
    }

    [[nodiscard]] static constexpr std::uint32_t unpack_index(TimerId id) noexcept {
        return static_cast<std::uint32_t>(id.value());
    }

    [[nodiscard]] static constexpr std::uint32_t unpack_generation(TimerId id) noexcept {
        return static_cast<std::uint32_t>(id.value() >> 32);
    }

    [[nodiscard]] static constexpr std::uint32_t next_generation(std::uint32_t generation) noexcept {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    [[nodiscard]] Slot* resolve_slot(TimerId id) noexcept {
        return const_cast<Slot*>(static_cast<TimerRegistrationTable const*>(this)->resolve_slot(id));
    }

    [[nodiscard]] Slot const* resolve_slot(TimerId id) const noexcept {
        if (!id.valid()) {
            return nullptr;
        }

        auto const index = static_cast<std::size_t>(unpack_index(id));
        if (index >= slots_.size()) {
            return nullptr;
        }

        Slot const& slot = slots_[index];
        if (!slot.active || slot.generation != unpack_generation(id) || slot.handler == nullptr) {
            return nullptr;
        }
        return &slot;
    }

    void release_slot(std::uint32_t index, Slot& slot) noexcept {
        assert(index < slots_.size());
        assert(slot.handler != nullptr);
        assert(slot.active);

        slot.handler = nullptr;
        slot.active = false;
        slot.generation = next_generation(slot.generation);
        free_.push_back(index);
    }

private:
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

} // namespace ddcs::io::detail
