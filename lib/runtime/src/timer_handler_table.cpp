#include "ddcs/runtime/detail/timer_handler_table.hpp"

#include <cassert>
#include <cstddef>

namespace ddcs::runtime::detail {

TimerId TimerHandlerTable::insert(TimerHandler* handler) {
    assert(handler != nullptr);

    std::uint32_t index{};
    if (free_.empty()) {
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(Slot{});
    } else {
        index = free_.back();
        free_.pop_back();
    }

    Slot& slot = slots_[index];
    slot.handler = handler;
    slot.active = true;
    return pack(index, slot.generation);
}

TimerHandler* TimerHandlerTable::consume(TimerId id) noexcept {
    Slot* slot = resolve_slot(id);
    if (slot == nullptr) {
        return nullptr;
    }

    TimerHandler* handler = slot->handler;
    release(unpack_index(id), *slot);
    return handler;
}

bool TimerHandlerTable::erase(TimerId id) noexcept {
    Slot* slot = resolve_slot(id);
    if (slot == nullptr) {
        return false;
    }

    release(unpack_index(id), *slot);
    return true;
}

TimerHandler* TimerHandlerTable::resolve(TimerId id) const noexcept {
    Slot const* slot = resolve_slot(id);
    return slot == nullptr ? nullptr : slot->handler;
}

TimerHandlerTable::Slot* TimerHandlerTable::resolve_slot(TimerId id) noexcept {
    return const_cast<Slot*>(static_cast<TimerHandlerTable const*>(this)->resolve_slot(id));
}

TimerHandlerTable::Slot const* TimerHandlerTable::resolve_slot(TimerId id) const noexcept {
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

void TimerHandlerTable::release(std::uint32_t index, Slot& slot) noexcept {
    slot.handler = nullptr;
    slot.active = false;
    slot.generation = next_generation(slot.generation);
    free_.push_back(index);
}

} // namespace ddcs::runtime::detail
