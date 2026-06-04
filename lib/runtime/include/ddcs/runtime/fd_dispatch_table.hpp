#pragma once

#include "ddcs/runtime/fd_handler.hpp"

#include <vector>

#include <cstdint>

namespace ddcs::runtime {

// fd -> FdHandler* 매핑 + per-fd generation. epoll data.u64 에 pack(fd,gen) 토큰을 싣는다.
//
// 목적: 같은 epoll 배치 안에서 이미 닫힌(또는 재사용된) fd의 stale 이벤트를 걸러낸다.
// erase 가 gen 을 올리므로, 그 이전에 발급된 토큰은 resolve에서 gen 불일치로 nullptr가 된다.
// -> 동일 배치 use-after-free 차단(핸들러가 그 자리에서 fd 를 닫아도 안전).
//
// fd로 직접 인덱싱(slots_[fd]). 한 fd는 add~del 동안 한 핸들러로 고정 - MOD는 interest만
// 바꾸고 핸들러는 그대로이므로, 재무장 토큰은 token(fd)로 fd만 가지고 복원한다.

/**
 * @brief fd와 FdHandler*의 매핑을 관리하는 클래스.
 *
 */
class FdDispatchTable {
public:
    // slot[fd].handler = handler (gen 유지). pack(fd, gen) 반환. fd 슬롯이 없으면 grow.
    std::uint64_t insert(int fd, FdHandler* handler);
    // 현재 토큰(MOD 재무장용). 전제: insert 된 fd.
    [[nodiscard]]
    std::uint64_t token(int fd) const noexcept;
    // slot[fd].handler = nullptr, ++gen. 멱등(미등록 fd 안전).
    void erase(int fd) noexcept;
    // gen 일치 && non-null -> handler, 아니면 nullptr.
    [[nodiscard]]
    FdHandler* resolve(std::uint64_t tok) const noexcept;

private:
    struct Slot {
        FdHandler* handler{nullptr};
        std::uint32_t gen{0};
    };

    [[nodiscard]]
    static constexpr std::uint64_t pack(int fd, std::uint32_t gen) noexcept {
        return (static_cast<std::uint64_t>(gen) << 32) | static_cast<std::uint32_t>(fd);
    }

    std::vector<Slot> slots_; // index = fd, lazy grow
};

} // namespace ddcs::runtime
