#pragma once

#include "ddcs/io/channel.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ddcs::io::detail {

// fd를 generation token으로 식별하는 Channel 등록 표
// - token에 담은 generation이 fd 재사용 전후의 등록을 구분한다.
class ChannelRegistry {
public:
    using Token = std::uint64_t;

    // token이 가리키는 Channel을 돌려준다.
    // fd 재사용 등으로 token이 stale하면 nullptr
    [[nodiscard]] Channel* resolve(Token token) const noexcept {
        Slot const* slot = resolve_slot(token);
        return slot == nullptr ? nullptr : slot->channel;
    }

    // 현재 등록된 channel의 token을 돌려준다. channel이 등록돼 있어야 한다.
    [[nodiscard]] Token token(Channel const& channel) const noexcept {
        assert(channel.valid());

        auto const index = static_cast<std::size_t>(channel.fd());
        assert(index < slots_.size());

        Slot const& slot = slots_[index];
        assert(slot.active && slot.channel == &channel);

        return pack(slot.generation, static_cast<std::uint32_t>(channel.fd()));
    }

    // channel을 fd 슬롯에 등록하고 token을 돌려준다.
    // 같은 fd를 다른 Channel이 점유 중이면 그 등록을 먼저 해제한다.
    [[nodiscard]] Token insert(Channel& channel) {
        assert(channel.valid());

        auto const index = static_cast<std::size_t>(channel.fd());
        if (index >= slots_.size()) {
            slots_.resize(index + 1);
        }

        Slot& slot = slots_[index];
        if (slot.active && slot.channel != &channel) {
            release_slot(slot);
        }

        slot.channel = &channel;
        slot.active = true;
        return pack(slot.generation, static_cast<std::uint32_t>(channel.fd()));
    }

    // channel 등록을 해제하고 generation을 올려 기존 token을 stale로 만든다.
    // 현재 등록이 아니면 false
    [[nodiscard]] bool erase(Channel& channel) noexcept {
        auto const index = static_cast<std::size_t>(channel.fd());
        if (index >= slots_.size()) {
            return false;
        }

        Slot& slot = slots_[index];
        if (!slot.active || slot.channel != &channel) {
            return false;
        }
        release_slot(slot);
        return true;
    }

private:
    struct Slot {
        Channel* channel = nullptr;
        std::uint32_t generation = 1;
        bool active = false;
    };

    [[nodiscard]] Slot const* resolve_slot(Token token) const noexcept {
        auto const index = static_cast<std::size_t>(unpack_fd(token));
        if (index >= slots_.size()) {
            return nullptr;
        }

        Slot const& slot = slots_[index];
        if (!slot.active || slot.generation != unpack_generation(token) ||
            slot.channel == nullptr) {
            return nullptr;
        }
        return &slot;
    }

    void release_slot(Slot& slot) noexcept {
        assert(slot.channel != nullptr);
        assert(slot.active);

        slot.channel = nullptr;
        slot.active = false;
        slot.generation = next_generation(slot.generation);
    }

    [[nodiscard]] static constexpr Token pack(std::uint32_t generation, std::uint32_t fd) noexcept {
        return (static_cast<Token>(generation) << 32) | fd;
    }

    [[nodiscard]] static constexpr std::uint32_t unpack_fd(Token token) noexcept {
        return static_cast<std::uint32_t>(token);
    }

    [[nodiscard]] static constexpr std::uint32_t unpack_generation(Token token) noexcept {
        return static_cast<std::uint32_t>(token >> 32);
    }

    [[nodiscard]] static constexpr std::uint32_t
    next_generation(std::uint32_t generation) noexcept {
        ++generation;
        // generation 0은 기본 token과 겹치므로 건너뛴다.
        return generation == 0 ? 1 : generation;
    }

    std::vector<Slot> slots_;
};

} // namespace ddcs::io::detail
