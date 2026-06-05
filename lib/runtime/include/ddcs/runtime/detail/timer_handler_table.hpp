#pragma once

#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"

#include <vector>

#include <cstdint>

namespace ddcs::runtime::detail {

// TimerId를 TimerHandler로 해석하고, cancel 또는 expiry 뒤의 stale id를 거른다.
// CAUTION: consume()과 erase()는 generation을 올린다. 예전 TimerId가 재사용된 slot으로 해석되지 않게 하기 위해서다.
class TimerHandlerTable {
public:
    [[nodiscard]]
    TimerId insert(TimerHandler* handler);
    [[nodiscard]]
    TimerHandler* consume(TimerId id) noexcept;
    bool erase(TimerId id) noexcept;

    [[nodiscard]]
    TimerHandler* resolve(TimerId id) const noexcept;
    [[nodiscard]]
    bool contains(TimerId id) const noexcept {
        return resolve(id) != nullptr;
    }

private:
    struct Slot {
        TimerHandler* handler{nullptr};
        std::uint32_t generation{1};
        bool active{false};
    };

    [[nodiscard]]
    static constexpr TimerId pack(std::uint32_t index, std::uint32_t generation) noexcept {
        return TimerId{(static_cast<std::uint64_t>(generation) << 32) | index};
    }

    [[nodiscard]]
    static constexpr std::uint32_t unpack_index(TimerId id) noexcept {
        return static_cast<std::uint32_t>(id.value());
    }

    [[nodiscard]]
    static constexpr std::uint32_t unpack_generation(TimerId id) noexcept {
        return static_cast<std::uint32_t>(id.value() >> 32);
    }

    [[nodiscard]]
    static constexpr std::uint32_t next_generation(std::uint32_t generation) noexcept {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    [[nodiscard]]
    Slot* resolve_slot(TimerId id) noexcept;
    [[nodiscard]]
    Slot const* resolve_slot(TimerId id) const noexcept;
    void release(std::uint32_t index, Slot& slot) noexcept;

private:
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

} // namespace ddcs::runtime::detail
