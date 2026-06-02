#include "ddcs/io/handler_table.hpp"

#include <cstddef>
#include <cstdint>

namespace ddcs::io {

std::uint64_t HandlerTable::insert(int fd, IoHandler* handler) {
    auto const idx = static_cast<std::size_t>(fd);
    if (idx >= slots_.size()) {
        slots_.resize(idx + 1);
    }
    slots_[idx].handler = handler;
    return pack(fd, slots_[idx].gen); // insert 는 gen 을 올리지 않는다(erase 가 올림)
}

std::uint64_t HandlerTable::token(int fd) const noexcept {
    auto const idx = static_cast<std::size_t>(fd);
    auto const gen = idx < slots_.size() ? slots_[idx].gen : std::uint32_t{0};
    return pack(fd, gen);
}

void HandlerTable::erase(int fd) noexcept {
    auto const idx = static_cast<std::size_t>(fd);
    if (idx >= slots_.size()) {
        return; // 미등록 - 멱등
    }
    slots_[idx].handler = nullptr;
    ++slots_[idx].gen; // 이전 incarnation 의 토큰을 전부 무효화
}

IoHandler* HandlerTable::resolve(std::uint64_t tok) const noexcept {
    auto const idx = static_cast<std::size_t>(static_cast<std::uint32_t>(tok)); // 하위 32 = fd
    auto const gen = static_cast<std::uint32_t>(tok >> 32);
    if (idx >= slots_.size()) {
        return nullptr;
    }
    auto const& slot = slots_[idx];
    if (slot.handler != nullptr && slot.gen == gen) {
        return slot.handler;
    }
    return nullptr;
}

} // namespace ddcs::io
