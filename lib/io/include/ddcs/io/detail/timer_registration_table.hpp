#pragma once

#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ddcs::io::detail {

// TimerId를 TimerHandler 등록으로 식별하는 표
//   cancel이나 expiry로 슬롯이 풀리면 generation이 올라 이전 id는 stale이 된다.
class TimerRegistrationTable {
public:
    [[nodiscard]] bool contains(TimerId id) const noexcept {
        return resolve_slot(id) != nullptr;
    }

    // id가 가리키는 handler를 돌려준다.
    // id가 stale하거나 미등록이면 nullptr
    [[nodiscard]] TimerHandler* resolve(TimerId id) const noexcept {
        Slot const* slot = resolve_slot(id);
        return slot == nullptr ? nullptr : slot->handler;
    }

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

    // id의 handler를 돌려주면서 등록을 해제한다.
    // 같은 id는 한 번만 성공하고 이후 nullptr
    [[nodiscard]] TimerHandler* consume(TimerId id) noexcept {
        Slot* slot = resolve_slot(id);
        if (slot == nullptr) {
            return nullptr;
        }

        TimerHandler* handler = slot->handler;
        release_slot(unpack_index(id), *slot);
        return handler;
    }

    // id 등록을 해제하고 generation을 올려 기존 id를 stale로 만든다.
    // 이미 풀렸거나 stale한 id면 false
    [[nodiscard]] bool erase(TimerId id) noexcept {
        Slot* slot = resolve_slot(id);
        if (slot == nullptr) {
            return false;
        }

        release_slot(unpack_index(id), *slot);
        return true;
    }

private:
    struct Slot {
        TimerHandler* handler = nullptr;
        std::uint32_t generation = 1;
        bool active = false;
    };

    [[nodiscard]] Slot* resolve_slot(TimerId id) noexcept {
        return const_cast<Slot*>(
            static_cast<TimerRegistrationTable const*>(this)->resolve_slot(id)
        );
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

    [[nodiscard]] static constexpr TimerId
    pack(std::uint32_t generation, std::uint32_t index) noexcept {
        return TimerId{(static_cast<std::uint64_t>(generation) << 32) | index};
    }

    [[nodiscard]] static constexpr std::uint32_t unpack_index(TimerId id) noexcept {
        return static_cast<std::uint32_t>(id.get());
    }

    [[nodiscard]] static constexpr std::uint32_t unpack_generation(TimerId id) noexcept {
        return static_cast<std::uint32_t>(id.get() >> 32);
    }

    [[nodiscard]] static constexpr std::uint32_t
    next_generation(std::uint32_t generation) noexcept {
        ++generation;
        // generation 0은 기본 id와 겹치므로 건너뛴다.
        return generation == 0 ? 1 : generation;
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

} // namespace ddcs::io::detail
