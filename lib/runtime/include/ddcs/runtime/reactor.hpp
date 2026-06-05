#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/runtime/detail/fd_handler_table.hpp"
#include "ddcs/runtime/fd_handler.hpp"

#include <chrono>
#include <optional>

#include <cstddef>
#include <cstdint>

struct epoll_event;

namespace ddcs::runtime {

// Edge-triggered epoll 기반 single-thread fd readiness reactor.
// NOTE: timer, signal, connection, frame 같은 상위 의미를 알지 않는다.
// CAUTION: epoll data.u64에는 generation token을 저장해서 같은 batch의 stale fd event를 거른다.
class Reactor {
public: // 특수 멤버 함수
    Reactor();
    ~Reactor();

    Reactor(Reactor const&) = delete;
    Reactor& operator=(Reactor const&) = delete;
    Reactor(Reactor&&) noexcept = delete;
    Reactor& operator=(Reactor&&) noexcept = delete;

public: // fd 등록
    [[nodiscard]]
    bool add(int fd, std::uint32_t interest, FdHandler* handler);
    // NOTE: mod()는 interest만 갱신한다. handler는 add()부터 del()까지 고정이다.
    [[nodiscard]]
    bool mod(int fd, std::uint32_t interest);
    void del(int fd);

public: // 루프
    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop() noexcept;
    bool running() const noexcept { return running_; }

private:
    [[nodiscard]]
    std::optional<int> wait(std::chrono::milliseconds timeout, epoll_event* events, std::size_t capacity);
    void dispatch(epoll_event const* events, int count);

private:
    common::Fd epoll_fd_{};
    bool running_{false};
    detail::FdHandlerTable fd_handlers_;
};

} // namespace ddcs::runtime
