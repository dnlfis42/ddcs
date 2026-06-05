#pragma once

#include "ddcs/runtime/fd_handler.hpp"

#include <vector>

#include <cstdint>

namespace ddcs::runtime::detail {

// fd 번호를 FdHandler로 해석하고, fd 재사용 뒤의 stale epoll token을 거른다.
// CAUTION: erase()는 generation을 올린다. 삭제 전에 반환된 이벤트가 재사용된 fd로 해석되지 않게 하기 위해서다.
class FdHandlerTable {
public:
    [[nodiscard]]
    std::uint64_t insert(int fd, FdHandler* handler);
    [[nodiscard]]
    std::uint64_t token(int fd) const noexcept;
    void erase(int fd) noexcept;

    [[nodiscard]]
    FdHandler* resolve(std::uint64_t token) const noexcept;

private:
    struct Slot {
        FdHandler* handler{nullptr};
        std::uint32_t generation{1};
        bool active{false};
    };

    [[nodiscard]]
    static constexpr std::uint64_t pack(std::uint32_t fd, std::uint32_t generation) noexcept {
        return (static_cast<std::uint64_t>(generation) << 32) | fd;
    }

    [[nodiscard]]
    static constexpr std::uint32_t unpack_fd(std::uint64_t token) noexcept {
        return static_cast<std::uint32_t>(token);
    }

    [[nodiscard]]
    static constexpr std::uint32_t unpack_generation(std::uint64_t token) noexcept {
        return static_cast<std::uint32_t>(token >> 32);
    }

    [[nodiscard]]
    static constexpr std::uint32_t next_generation(std::uint32_t generation) noexcept {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    [[nodiscard]]
    Slot* resolve_slot(std::uint64_t token) noexcept;
    [[nodiscard]]
    Slot const* resolve_slot(std::uint64_t token) const noexcept;
    void release(Slot& slot) noexcept;

private:
    std::vector<Slot> slots_;
};

} // namespace ddcs::runtime::detail
