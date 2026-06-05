#include "ddcs/runtime/detail/fd_handler_table.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace ddcs::runtime::detail {

std::uint64_t FdHandlerTable::insert(int fd, FdHandler* handler) {
    assert(fd >= 0);
    assert(handler != nullptr);

    auto const index = static_cast<std::size_t>(fd);
    if (index >= slots_.size()) {
        slots_.resize(index + 1);
    }

    Slot& slot = slots_[index];
    slot.handler = handler;
    slot.active = true;
    return pack(static_cast<std::uint32_t>(fd), slot.generation);
}

std::uint64_t FdHandlerTable::token(int fd) const noexcept {
    auto const index = static_cast<std::size_t>(fd);
    auto const generation = index < slots_.size() ? slots_[index].generation : std::uint32_t{};
    return pack(static_cast<std::uint32_t>(fd), generation);
}

void FdHandlerTable::erase(int fd) noexcept {
    auto const index = static_cast<std::size_t>(fd);
    if (index >= slots_.size() || !slots_[index].active) {
        return;
    }

    release(slots_[index]);
}

FdHandler* FdHandlerTable::resolve(std::uint64_t token) const noexcept {
    Slot const* slot = resolve_slot(token);
    return slot == nullptr ? nullptr : slot->handler;
}

FdHandlerTable::Slot* FdHandlerTable::resolve_slot(std::uint64_t token) noexcept {
    return const_cast<Slot*>(static_cast<FdHandlerTable const*>(this)->resolve_slot(token));
}

FdHandlerTable::Slot const* FdHandlerTable::resolve_slot(std::uint64_t token) const noexcept {
    auto const index = static_cast<std::size_t>(unpack_fd(token));
    if (index >= slots_.size()) {
        return nullptr;
    }

    Slot const& slot = slots_[index];
    if (!slot.active || slot.generation != unpack_generation(token) || slot.handler == nullptr) {
        return nullptr;
    }
    return &slot;
}

void FdHandlerTable::release(Slot& slot) noexcept {
    slot.handler = nullptr;
    slot.active = false;
    slot.generation = next_generation(slot.generation);
}

} // namespace ddcs::runtime::detail
