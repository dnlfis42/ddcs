#pragma once

#include "ddcs/io/channel.hpp"

#include <vector>

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace ddcs::io::detail {

// Channel fd를 generation token으로 해석해 fd 재사용 뒤의 stale event를 거른다.
class ChannelRegistry {
public:
    using Token = std::uint64_t;

public:
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

    [[nodiscard]] Token token(Channel const& channel) const noexcept {
        assert(channel.valid());

        auto const index = static_cast<std::size_t>(channel.fd());
        auto const generation = index < slots_.size() ? slots_[index].generation : std::uint32_t{};
        return pack(generation, static_cast<std::uint32_t>(channel.fd()));
    }

    bool erase(Channel& channel) noexcept {
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

    [[nodiscard]] Channel* resolve(Token token) const noexcept {
        Slot const* slot = resolve_slot(token);
        return slot == nullptr ? nullptr : slot->channel;
    }

private:
    struct Slot {
        Channel* channel{nullptr};
        std::uint32_t generation{1};
        bool active{false};
    };

private:
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
        return generation == 0 ? 1 : generation;
    }

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

private:
    std::vector<Slot> slots_;
};

} // namespace ddcs::io::detail
